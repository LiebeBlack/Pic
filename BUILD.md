# ARTPICST - Guía de Compilación

## Requisitos

- Windows 10/11
- Visual Studio 2019/2022 con C++ (Build Tools o Community/Professional/Enterprise)
- Git (para clonar el repositorio)

## Compilación Rápida

### Opción 1: PowerShell (Recomendado)
```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```

### Opción 2: Batch
```cmd
build.bat
```

### Opción 3: Manual
Si prefieres compilar cada componente por separado. **El orden importa**: el
instalador incrusta los binarios como recursos RCDATA, así que hay que compilar
primero visor y updater, poblar `resources\app\` y solo entonces compilarlo.

#### 1. Recursos (visor, updater e instalador)
```cmd
cd C:\Users\Admin\Documents\GitHub\Pic
if not exist build mkdir build
rc /nologo /fo build\artpicst.res artpicst.rc
cd updater
if not exist build mkdir build
rc /nologo /fo ..\build\artpicst_updater.res artpicst_updater.rc
cd ..\installer
rc /nologo /fo ..\build\artpicst_installer.res artpicst_installer.rc
cd ..
```

#### 2. Programa Principal (visor)

> **Warnings estrictos**: los tres objetivos se compilan con `/W4 /permissive-`
> (MSVC) o `-Wall -Wextra` (MinGW). `/wd4324` solo silencia el aviso de
> alineación intencionada de `PixelBlock`. No añadas supresiones nuevas sin
> justificación en un comentario.

```cmd
cl /nologo /EHsc /std:c++latest /O2 /Ob3 /Oi /GL /Gy /utf-8 /W4 /permissive- /wd4324 /I. /Iinclude /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DSTBI_WINDOWS_UTF8 /D_WIN32_WINNT=0x0601 /D_CRT_SECURE_NO_WARNINGS /Fe:"build\artpicst.exe" src\main.cpp build\artpicst.res /link gdiplus.lib user32.lib kernel32.lib shell32.lib shlwapi.lib gdi32.lib msimg32.lib ole32.lib oleaut32.lib uuid.lib dwmapi.lib windowscodecs.lib comdlg32.lib d2d1.lib dwrite.lib advapi32.lib /MANIFEST:EMBED /MANIFESTINPUT:artpicst.manifest /SUBSYSTEM:WINDOWS /LTCG /OPT:REF /OPT:ICF
```

#### 3. Updater (auto-actualizador)
```cmd
cd updater
cl /nologo /EHsc /std:c++latest /O2 /Ob3 /Oi /utf-8 /W4 /permissive- /wd4324 /I. /I..\installer /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /D_CRT_SECURE_NO_WARNINGS /Fe:"..\build\artpicst_updater.exe" artpicst_updater.cpp ..\build\artpicst_updater.res /link winhttp.lib gdiplus.lib shell32.lib shlwapi.lib user32.lib advapi32.lib gdi32.lib ole32.lib uuid.lib dwmapi.lib /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF
cd ..
```

Necesita `installer\version.hpp` (`/I..\installer`) para reutilizar las
constantes del registro y el comparador de versiones. Después de compilarlo,
ejecuta el selftest como control de calidad:

```cmd
build\artpicst_updater.exe --selftest
```

El selftest incluye, además del comparador de versiones y el parser JSON, una
**matriz de regresión de seguridad** (JSON truncado, escapes `\u` al final de
cadena, cadenas sin cierre, assets corruptos) y una campaña de **fuzzing
determinista** (4.000 mutaciones por defecto, ampliable con la variable de
entorno `ARTPICST_FUZZ_ITERS=N`).

## Tests y fuzzing (tests\)

Dos ejecutables de consola independientes, sin dependencias de Windows-UI:

### 1. Núcleo de imagen (`tests\image_core_test.cpp`)

Valida `src\image_core.hpp` tal cual lo usa el visor: LUT constexpr contra la
fórmula original en coma flotante, allocator alineado a 64 B (alineación,
realloc, rutas degeneradas), premultiplicado alfa (ruta SIMD contra referencia
por bytes), detección de transparencia, horneado de efectos (gris BT.601,
negativo), transformaciones geométricas (90/180/flip/combinados contra
referencia directa), baldosa de ajedrez y fuzzing del allocator.

```cmd
cl /nologo /EHsc /std:c++20 /O2 /utf-8 /W4 /permissive- /I. /Fe:"build\image_core_test.exe" tests\image_core_test.cpp /link kernel32.lib
build\image_core_test.exe
:: Fuzzing del allocator ampliado:
set ARTPICST_TEST_FUZZ_ITERS=100000 && build\image_core_test.exe
```

### 2. Fuzzer del parser JSON (`tests\release_json_fuzzer.cpp`)

Martillea `installer\release_json.hpp` con tres familias de entradas
adversariales: mutaciones del documento válido, **todos** los truncamientos
posibles (exhaustivo) y basura binaria pseudoaleatoria. Verifica terminación
(sin cuelgues), consistencia (`valid` implica tag no vacío) y límites de
`size`. 100 % determinista (semilla fija): cualquier fallo es reproducible.

```cmd
cl /nologo /EHsc /std:c++20 /O2 /utf-8 /W4 /permissive- /I. /Fe:"build\release_json_fuzzer.exe" tests\release_json_fuzzer.cpp /link kernel32.lib
build\release_json_fuzzer.exe            :: 50.000 iteraciones/familia
build\release_json_fuzzer.exe 200000     :: campaña ampliada
```

Los tres scripts de build (`build.ps1`, `build.bat`, `build_mingw.bat`) y el
gate de CI compilan y ejecutan ambos como paso `[3b/5]`, tras el selftest del
updater y antes de empaquetar el instalador.

### 3. Orientación EXIF (`tests\jpeg_exif_test.cpp`)

Ejecuta EXACTAMENTE el mismo código que el visor (`src\jpeg_exif.hpp`, extraído
de `main.cpp`) contra JPEGs sintéticos construidos en memoria: orientaciones
1-8 válidas (little y big endian), offsets del IFD con desbordamiento `uint32_t`
(los casos de la auditoría: `0xFFFFFFFE`, `0xFFFFFFFF`), `tagCount` gigante,
EXIF truncado, TIFF corto, firma ausente y no-JPEG. Contrato fijado: cualquier
fallo devuelve **1** ("normal"), nunca un valor fuera de 1..8.

```cmd
cl /nologo /EHsc /std:c++20 /O2 /utf-8 /W4 /permissive- /I. /Fe:"build\jpeg_exif_test.exe" tests\jpeg_exif_test.cpp /link kernel32.lib shell32.lib
build\jpeg_exif_test.exe
```

### Fuzzing nocturno con AddressSanitizer (CI)

`.github/workflows/nightly-fuzz.yml` se ejecuta a diario (03:00 UTC) o bajo
demanda (`workflow_dispatch`) y compila los tres ejecutables con
`/fsanitize=address` (MSVC), estirando las campañas: parser JSON con 200.000
iteraciones, EXIF con offsets hostiles y allocator con 50.000 allocs. Cualquier
lectura fuera de límites, use-after-free o leak falla el job con el stack
del sanitizer — la capa de proof-of-memory-safety que los tests solos no dan.

#### 4. Payload autocontenido
```cmd
python scripts\collect_app_payload.py --root .
```

Copia `artpicst.exe`, `artpicst_updater.exe`, `artpicst.ico`, `version.json` y
`README.md` a `resources\app\`, desde donde el `.rc` del instalador los incrusta.

#### 5. Instalador autocontenido
```cmd
cd installer
cl /nologo /EHsc /std:c++latest /O2 /Ob3 /Oi /utf-8 /W4 /I. /I..\include /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /Fe:"build\artpicst_installer.exe" artpicst_installer.cpp ..\build\artpicst_installer.res /link gdiplus.lib shlwapi.lib shell32.lib comctl32.lib dwmapi.lib user32.lib advapi32.lib gdi32.lib ole32.lib uuid.lib /SUBSYSTEM:WINDOWS "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'" /OPT:REF /OPT:ICF
cd ..
```

## Icono multirresolución

El `.ico` del repositorio ya está generado con todas las resoluciones (16 a 256 px),
así que **no hace falta regenerarlo para compilar**. Si cambias el diseño, vuelve a
generarlo (sólo necesita Python; no usa Pillow ni ninguna dependencia externa):

```cmd
python scripts\generate_icon.py            # regenera resources\artpicst.ico
python scripts\generate_icon.py --preview  # + vista previa PNG 256x256
```

## Compilación con MinGW-W64 (alternativa a MSVC)

```cmd
build_mingw.bat
```

Compila con `g++ -std=c++23 -O3 -funroll-loops`. Si `windres` falla con
`gcc: fatal error: cannot execute 'cc1'`, indica el preprocesador explícitamente:

```cmd
windres -i artpicst.rc -o build\artpicst.res -O coff -I. ^
        --preprocessor="gcc -E -xc -DRC_INVOKED"
```

## Archivos Generados

Después de la compilación exitosa, encontrarás los siguientes archivos en el directorio `dist\`:

- `artpicst.exe` - Programa principal (visuales premium y rendimiento optimizado)
- `artpicst_updater.exe` - Módulo de auto-actualización (también dentro del instalador)
- `artpicst_installer.exe` - Instalador autocontenido (visor + updater incrustados)
- `artpicst.ico` - Icono de la aplicación
- `version.json` - Información de versión

Para publicar: renombra `artpicst_installer.exe` a **`artpicst-installer.exe`**
(ese es el nombre exacto del asset que el auto-actualizador busca en GitHub
Releases). El workflow de CI hace esto automáticamente junto al ZIP portable.

## Mejoras Implementadas

### Interfaz del Programa Principal
- ✅ Efectos glassmorphism/acrylic mejorados
- ✅ Mayor transparencia y blur premium
- ✅ Colores más vibrantes y modernos
- ✅ Zoom de máxima calidad con auto-snap 100%
- ✅ Calidad de renderizado bicúbico fotográfico
- ✅ Navegación por teclado optimizada (↑↓ zoom, ←→ imágenes)
- ✅ Consumo ultra-ligero de memoria (RAM ≤ 80 MB, CPU ~0%)

### Instalador (wizard GDI+ autocontenido)
- ✅ Ventana 720×560 redimensionable y centrada, tema pitch-black (#000000 / #0A0A0A,
  acentos #00F0FF / #7000FF), doble búfer y sin parpadeo
- ✅ Payload autocontenido: el `.exe` incrusta visor, updater, icono, `version.json`
  y README como recursos RCDATA — un único archivo distribuible
- ✅ Instalación en hilo separado con `PostMessage`: la UI nunca se congela
- ✅ Duración exacta de **34 segundos** con 10 fases técnicas en vivo (progreso y
  consola de log con autoscroll, marca de tiempo `t = X.X s` por línea)
- ✅ Elevación por UAC (manifiesto `requireAdministrator` + relanzado con `runas`),
  con degradación a instalación por usuario si se rechaza
- ✅ Instalación **tradicional para todos los usuarios** en `%ProgramFiles%\ARTPICST`
- ✅ Accesos directos en el Menú Inicio (todos los usuarios) y en el Escritorio
- ✅ Registro completo: `Uninstall\ARTPICST` (HKLM/HKCU), `App Paths`, ProgID,
  `RegisteredApplications` y asociaciones por `OpenWithProgids` **sin secuestrar**
  el programa predeterminado del usuario (las miniaturas del Explorador se conservan)
- ✅ Desinstalador con wizard gráfico propio (14 segundos, 10 fases, misma estética),
  opción de conservar la configuración del usuario, limpieza de accesos directos,
  claves y carpeta, y borrado diferido del propio ejecutable
- ✅ Modo actualización silencioso (`--update <payload> --dir <dir>`) usado por el
  auto-actualizador, con relanzamiento automático de la app
- ✅ Reversión automática (rollback) si la instalación falla a medias

### Auto-Actualizador (`updater/`)
- ✅ Consulta asíncrona a `https://api.github.com/repos/LiebeBlack/Pic/releases/latest`
  vía WinHTTP, con tolerancia total a fallos (sin red o rate-limit ⇒ silencio)
- ✅ Comparador de versiones propio (`installer/version.hpp`): soporta prefijo `v`,
  prereleases y números de más de un dígito (`auto-59` > `auto-9`)
- ✅ Notificación flotante minimalista con **Instalar ahora** / **Más tarde** y
  checkbox para instalar futuras versiones automáticamente
- ✅ Descarga con progreso real y verificación de integridad SHA-256
- ✅ Instalación encadenada: `artpicst_installer.exe --update <payload> --dir <dir>`
  con reinicio automático del visor
- ✅ CLI: `--check`, `--forced`, `--background`, `--selftest` (gate en CI)
- ✅ Integración con el visor: chequeo diario en segundo plano y menú
  "Buscar actualizaciones..."

## Solución de Problemas

### Error: "Visual Studio not found"
Instala Visual Studio Build Tools con C++:
https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022

Alternativa sin Visual Studio: `build_mingw.bat` usa MinGW-w64 (GCC 13+, con
`windres` en el PATH). El código es común a ambos toolchains.

### Error: "cl not recognized"
Abre "Developer Command Prompt for VS" desde el menú de inicio y ejecuta el script de compilación desde allí.

### Error de compilación
Asegúrate de tener todas las dependencias:
- Windows SDK
- C++ tools
- GDI+ libraries (incluidas en Windows)

## Preparación para GitHub

1. Compila el proyecto usando build.ps1 o build.bat
2. Verifica que los archivos estén en `dist\`
3. Crea un release en GitHub
4. Sube **`artpicst-installer.exe`** (el instalador autocontenido renombrado) como
   asset principal: es el archivo que el auto-actualizador descarga. Opcionalmente
   añade `artpicst-portable.zip`.

También puedes dejar que CI lo haga todo: `.github/workflows/release.yml` compila
los 3 binarios, valida el updater con `--selftest`, prepara el payload, compila el
instalador y publica `artpicst-installer.exe` + `artpicst-portable.zip` en cada
push a `main` y en cada tag `v*`.