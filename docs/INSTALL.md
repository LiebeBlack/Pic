# ARTPICST — Instalador, distribución y desinstalación

El instalador es un **ejecutable autocontenido**: `artpicst-installer.exe`
incrusta el visor, el auto-actualizador, el icono, `version.json` y el README como
recursos `RCDATA`. No necesita NSIS, WiX ni ningún runtime externo: es un único
archivo distribuible.

---

## 1. Estructura del instalador

| Elemento | Implementación |
| --- | --- |
| Wizard gráfico | GDI+ con doble búfer, ventana 720×560 centrada y redimensionable |
| Tema | *Pitch black* (`#000000` / `#0A0A0A`, acentos `#00F0FF` / `#7000FF`) |
| Payload | 5 recursos `RCDATA` (201..205): visor, icono, `version.json`, README, updater |
| Progreso | 10 fases técnicas con consola de log en vivo y marca temporal `t = X.X s` |
| Concurrencia | Instalación en hilo aparte con `PostMessage`: la UI nunca se congela |
| Elevación | Manifiesto `requireAdministrator` + relanzado con `runas`; si se rechaza, degrada a instalación por usuario |
| Reversión | Rollback automático si una fase falla a medias |

### Fases de la instalación

1. Preparación y validación del payload incrustado.
2. Diagnóstico del sistema: **CPU con SSE4.2**, versión de Windows, DPI y
   Microsoft Visual C++ 2015-2022 x64 Redistributable.
3. Verificación del directorio de destino.
4. Extracción del payload.
5. Escritura del visor y del updater.
6. Asociaciones de archivo (`OpenWithProgids`, **sin secuestrar** el programa
   predeterminado del usuario).
7. Accesos directos.
8. Registro del desinstalador (`Uninstall\ARTPICST`, `App Paths`, ProgID,
   `RegisteredApplications`).
9. Registro de `EstimatedSize` y metadatos para «Aplicaciones instaladas».
10. Verificación final y limpieza.

---

## 2. Requisitos previos (verificación de dependencias)

El instalador **comprueba y comunica** cada requisito antes de tocar el sistema,
en lugar de dejar una instalación que falle al arrancar:

| Requisito | Comprobación | Si falta |
| --- | --- | --- |
| **CPU con SSE4.2** | `CPUID` hoja 1, `ECX` bit 20 (`CpuSupportsSse42`) | Aborta con mensaje claro: se necesita Intel Nehalem / AMD Bulldozer (2008+) o posterior |
| **Windows 10 o superior** | `RtlGetVersion` de `ntdll` (build ≥ 10240) | Aborta indicando la versión requerida |
| **VC++ 2015-2022 x64 Redistributable** | Claves `…\VC\Runtimes\x64\Installed` y `vcruntime140.dll` | Lo instala en silencio si viene en el payload; si no, informa y continúa |
| **Arquitectura x64** | Manifiesto y binarios x64 | Windows bloquea el ejecutable |

**Por qué SSE4.2 es un requisito duro.** Los tres binarios se compilan con
baseline SSE4.2 (`/arch:SSE4.2` / `-msse4.2`), así que una CPU anterior ejecutaría
una instrucción ilegal al arrancar. El instalador lo detecta **antes** de instalar
y lo explica. Ver `docs/PERFORMANCE.md` para la justificación del baseline.

**Verificación de la versión de Windows.** `GetVersionEx` y `VerifyVersionInfo`
devuelven datos falseados según el manifiesto (`supportedOS`), por lo que se usa
`RtlGetVersion` de `ntdll.dll`, que siempre informa de la verdad. Si el build no
puede determinarse, se avisa y se continúa en lugar de bloquear.

---

## 3. Selección de la ruta de instalación

- **Por defecto:** `%ProgramFiles%\ARTPICST` (instalación tradicional para todos
  los usuarios, `HKLM`).
- **Selección manual:** botón *Examinar* (`SHBrowseForFolderW`) en el paso
  *Destino*, con validación de la ruta elegida.
- **Degradación:** si el usuario rechaza la elevación UAC, se instala solo para el
  usuario actual (`%LOCALAPPDATA%`, `HKCU`).
- **Modo silencioso:** `--dir "<ruta>"` permite fijar el destino sin interfaz.

---

## 4. Accesos directos

| Ubicación | Alcance | Elemento |
| --- | --- | --- |
| Escritorio | Todos los usuarios o usuario actual | `ARTPICST.lnk` → `artpicst.exe` (opcional, casilla en el wizard) |
| Menú Inicio | Todos los usuarios | `ARTPICST.lnk` → `artpicst.exe` |
| Menú Inicio | Todos los usuarios | `Uninstall ARTPICST.lnk` → `artpicst_installer.exe --uninstall` |

Los enlaces se crean con `IShellLinkW` (con directorio de trabajo correcto, para
que el visor encuentre su icono y sus rutas relativas). La desinstalación los
elimina de **ambos** perfiles (usuario actual y *Todos los usuarios*), porque una
instalación antigua por usuario puede haber dejado restos.

---

## 5. Modos desatendidos (scripts y CI)

```bat
artpicst_installer.exe --silent                          :: instalación desatendida
artpicst_installer.exe --silent --dir "C:\MiCarpeta"     :: a una ruta concreta
artpicst_installer.exe --silent --uninstall              :: desinstalación desatendida
artpicst_installer.exe --update <payload.exe> --dir <dir> :: usado por el auto-actualizador
```

### Códigos de salida

| Código | Significado |
| --- | --- |
| `0` | Correcto |
| `1` | Fallo genérico (con reversión automática del estado previo) |
| `2` | Payload incompleto o corrupto |
| `3` | Archivos en uso: cerrar ARTPICST y reintentar |

En modo silencioso no se muestra ninguna ventana; el resultado se comunica
únicamente por el código de salida, lo que permite encadenarlo desde CI.

---

## 6. Desinstalación limpia

El desinstalador es el propio instalador (`--uninstall`) y muestra un wizard
gráfico con la misma estética (10 fases). Elimina:

1. Accesos directos del Escritorio y del Menú Inicio (perfil propio y *Todos los
   usuarios*).
2. **Las dos raíces del registro**: `HKLM` y `HKCU` (para retirar también
   instalaciones antiguas por usuario): `Uninstall\ARTPICST`, `App Paths`, ProgID,
   `RegisteredApplications` y `OpenWithProgids`.
3. Todos los archivos de la carpeta de instalación.
4. El directorio del Menú Inicio si queda vacío.
5. **El propio ejecutable en uso**: se mueve a `%TEMP%` y se programa su borrado
   final junto con el directorio (`rd /s /q`), con `SHChangeNotify` para refrescar
   el shell.

**Conservación opcional.** El wizard permite conservar la configuración del
usuario; en ese caso no se elimina `%LOCALAPPDATA%\ARTPICST`, que contiene
preferencias y el log de diagnóstico (`artpicst.log`, con rotación a
`artpicst.log.1` al superar 1 MB).

---

## 7. Registro en el sistema

| Clave | Contenido |
| --- | --- |
| `HKLM\…\Uninstall\ARTPICST` | `DisplayName`, `DisplayVersion`, `Publisher`, `DisplayIcon`, `UninstallString`, `QuietUninstallString`, `InstallLocation`, `URLInfoAbout`, `HelpLink`, `NoModify=1`, `NoRepair=1`, `EstimatedSize` |
| `HKLM\…\App Paths\artpicst.exe` | Ruta del ejecutable (permite `artpicst` desde *Ejecutar*) |
| ProgID + `RegisteredApplications` | Entrada en «Aplicaciones predeterminadas» |
| `OpenWithProgids` | Aparece en *Abrir con*, **sin** robar el predeterminado del usuario |

`EstimatedSize` alimenta la columna «Tamaño» de *Aplicaciones instaladas* y se
calcula recorriendo los archivos realmente instalados.

---

## 8. Actualización

El auto-actualizador descarga `artpicst-installer.exe` desde GitHub Releases,
verifica su **SHA-256** (fail-closed: si el hash no puede calcularse, se descarta)
y lo ejecuta con `--update <payload> --dir <carpeta>`. El instalador sustituye los
archivos, cierra el visor si sigue abierto y lo reinicia.

Renombra siempre `dist\artpicst_installer.exe` a **`artpicst-installer.exe`** al
publicar: ese es el nombre de asset exacto que busca el auto-actualizador.

---

## 9. Identidad en la barra de tareas

Cada rol fija su propio `AppUserModelID` (`SetCurrentProcessExplicitAppUserModelID`):

| Binario / modo | AppUserModelID |
| --- | --- |
| Visor | `LiebeBlack.ARTPICST.Viewer` |
| Instalador, desinstalador y modo actualización | `LiebeBlack.ARTPICST.Installer` |

Sin esto, Windows puede agrupar ventanas distintas bajo la misma entrada y perder
el icono y la miniatura correctos.

---

## 10. Compilar el instalador

El instalador **no puede compilarse antes que el payload**, porque su `.rc` lo
incrusta. El orden es obligatorio:

```cmd
make package                     :: genera resources/app/ y luego el instalador
cmake -B out -DARTPICST_PACKAGE=ON && cmake --build out --config Release
powershell -ExecutionPolicy Bypass -File build.ps1   :: pipeline completo
```

Con CMake y `ARTPICST_PACKAGE=OFF` (por defecto) solo se compilan el visor, el
updater y los tests: así un `cmake --build` en limpio funciona sin payload.

---

## 11. Firma digital

**No implementada en esta versión.** El build no firma los binarios, así que
Windows SmartScreen mostrará el aviso de editor desconocido. Para activarla:
obtén un certificado de firma de código, añade el paso de `signtool` al final de
`build.ps1` y del workflow de release, y firma en este orden: `artpicst.exe`,
`artpicst_updater.exe`, el payload ya recopilado y, por último, el instalador
autocontenido (firmar antes del que lo incrusta invalidaría la firma del
contenedor).
