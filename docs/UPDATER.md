# ARTPICST — Módulo Inteligente de Actualización (Auto-Sync)

Documentación técnica del sistema de auto-actualización: quién interviene, cómo
se comunican los hilos, qué políticas aplican y cómo funciona la cadena
completa *chequeo → notificación → descarga verificada → instalación → reinicio*.

---

## 1. Componentes

```
┌───────────────────────┐        ┌──────────────────────────┐
│  artpicst.exe (visor) │        │  artpicst_updater.exe    │
│                       │        │                          │
│  updater_client.hpp   │ Shell- │  · WinHTTP + hilo de red │
│  · StartBackground-   │ Execute│  · Parser JSON propio    │
│    DailyCheck()       │ ─────► │  · SHA-256 propio        │
│    (1×/día, 20-30 s)  │        │  · Comparador de versiones│
│  · CheckForUpdates-   │        │  · Notificación flotante │
│    Interactive()      │        │    (GDI+, 460×258)       │
└───────────────────────┘        └────────────┬─────────────┘
                                              │ ShellExecute
                                              ▼
                            ┌─────────────────────────────────────┐
                            │  artpicst-installer.exe (asset)     │
                            │  --update <payload> --dir <carpeta> │
                            │  · Staging .old / rollback          │
                            │  · Extracción RCDATA del payload    │
                            │  · Relanzamiento del visor          │
                            └─────────────────────────────────────┘
```

| Binario | Rol | Hilos |
|---|---|---|
| `artpicst.exe` | Dispara el chequeo diario y la entrada de menú | Hilo detached 20-30 s (solo registro + `ShellExecute`) |
| `artpicst_updater.exe` | Consulta, decide, notifica, descarga y encadena | Worker de red (`std::thread`) + hilo de descarga gestionado |
| `artpicst_installer.exe` | Instala el payload y reinicia la app | Pipeline en `std::thread` → `PostMessage` a la UI |

**Regla de oro:** ningún hilo de red toca nunca la interfaz. Toda la
comunicación UI ↔ worker es por `PostMessage` (mensajes `WM_APP+0x3x`) y
variables atómicas; los hilos de UI solo hacen `join()` de hilos cuando el
trabajo ya terminó.

---

## 2. Flujo completo

1. **Arranque del visor.** En `WM_CREATE` se lanza `StartBackgroundDailyCheck()`:
   un hilo detached espera 20-30 s (xorshift32 sobre el reloj) y, si la política
   de 1 chequeo/día lo permite, ejecuta `artpicst_updater.exe` (sin argumentos).
2. **Consulta.** El updater hace `GET https://api.github.com/repos/LiebeBlack/Pic/releases/latest`
   con WinHTTP (TLS, timeouts de 8 s, reintentos con espera lineal, cabeceras
   `User-Agent` y `Accept: application/vnd.github+json`).
3. **Parseo.** El parser JSON propio extrae `tag_name`, `body` y el bloque del
   asset `artpicst-installer.exe` (`browser_download_url`, `size`, `digest`).
4. **Comparación** (`installer/version.hpp` → compartida con el instalador):
   - Prefijo decorativo ignorado (`v`, `auto`).
   - Los dígitos son la componente numérica principal (`auto-100` > `auto-99`).
   - Sufijo prerelease lexicográfico solo como desempate (semver:
     `1.0.0` > `1.0.0-rc1`).
5. **Decisión.**
   - Sin red / 403 / 429 / JSON inválido → **silencio** (reintenta mañana).
   - Al día → silencio (o toast breve si el usuario forzó con `--forced`).
   - Versión nueva → **notificación flotante** (esquina inferior derecha,
     `WS_EX_NOACTIVATE`: nunca roba el foco).
6. **Notificación.** *Instalar actualización ahora* · *Recordar más tarde*
   (pospone 1 h sin gastar el chequeo diario) · checkbox *Instalar
   actualizaciones automáticas en segundo plano* (persiste en el registro).
7. **Descarga.** A `%LOCALAPPDATA%\ARTPICST\updates` con progreso real (bytes,
   velocidad media), guard de 60 s sin datos y verificación **SHA-256** si el
   release publica `digest` (`sha256:...`). Descarga en `.part` + rename: nunca
   queda un archivo a medias con nombre definitivo.
8. **Instalación encadenada.** Se ejecuta el instalador descargado con
   `--update <payload> --dir <dir>`; el instalador (manifiesto
   `requireAdministrator`) pide UAC él mismo, aparta los binarios actuales como
   `.old`, extrae el payload RCDATA, verifica los archivos y **relanza el visor**
   de-elevado (vía `explorer.exe`).

---

## 3. Políticas

| Política | Valor | Dónde |
|---|---|---|
| Frecuencia de chequeo | 1 / 24 h | `LastUpdateCheck` (QWORD unix) |
| Retraso tras arrancar | 20-30 s aleatorio | xorshift32 en `updater_client.hpp` |
| Posponer ("Más tarde") | 1 h | resta 3600 s de `LastUpdateCheck` |
| Cierre de la notificación | automático | 5 min de descarga máxima |
| Purga de descargas | > 7 días | `PurgeOldDownloads()` en cada arranque |
| Timeout de descarga | 60 s sin datos | `kDownloadTimeout` |

## 4. Seguridad

- **HTTPS obligatorio** (`WINHTTP_FLAG_SECURE`) en todas las peticiones.
- **SHA-256** del instalador descargado verificado contra el `digest` del
  release antes de ejecutarlo; si no coincide, se borra y se cancela.
- **Escalada explícita**: el updater nunca se eleva; la elevación la pide el
  instalador por manifiesto UAC. El visor relanzado tras la actualización se
  abre de-elevado (truco `explorer.exe`).
- **Rollback**: los binarios sustituidos se apartan como `*.old` y se restauran
  automáticamente si la instalación falla a medias.

## 5. Protocolo de mensajes (UI del updater)

| Mensaje | Valor | Significado |
|---|---|---|
| `WM_QUIT_APP` | `WM_APP+0x30` | cierre controlado desde el hilo de descarga |
| `WM_UPD_NETWORK` | `WM_APP+0x31` | sin conexión (solo en modo forzado) → toast |
| `WM_UPD_UPTODATE` | `WM_APP+0x32` | ya estás al día (forzado) → toast |
| `WM_UPD_CHECK_NO` | `WM_APP+0x33` | `--check`: sin actualización (exit 1) |
| `WM_UPD_CHECK_YES` | `WM_APP+0x34` | `--check`: hay actualización (exit 0) |
| `WM_UPD_FAILED` | `WM_APP+0x35` | fallo de descarga/instalación → toast + retry mañana |

Estado compartido worker ↔ UI: `phase` (atómico), `bytesDone` / `bytesTotal`
(atómicos), release y rutas protegidos por el ciclo de vida del hilo gestionado.

## 6. Registro (`HKLM` instalación máquina, `HKCU` por usuario)

```
Software\ARTPICST
  Version          = 1.2.0            (lo escribe el instalador)
  InstallDir       = C:\Program Files\ARTPICST
  LastUpdateCheck  = <unix QWORD>     (lo escribe el updater)
  AutoInstallUpdates = 1              (checkbox de la notificación)
```

## 7. CLI del updater

```bat
artpicst_updater.exe                :: chequeo + notificación flotante (usado por el visor)
artpicst_updater.exe --check        :: consulta y devuelve exit code (0=actualización, 1=al día, 3=sin red)
artpicst_updater.exe --check --forced
artpicst_updater.exe --background   :: descarga + instalación silenciosa (checkbox auto)
artpicst_updater.exe --selftest     :: autotest offline (gate de CI)
```

El `--selftest` valida el comparador de versiones (casos `auto-N`, semver,
prereleases), el parser JSON (tag, body, asset y digest) y el limpiador de
notas Markdown. `build.bat` / `build.ps1` / `build_mingw.bat` y la CI lo
ejecutan como **gate de calidad** antes de empaquetar.

## 8. Fallos y degradación

| Situación | Comportamiento |
|---|---|
| Sin conexión | Silencio; se reintenta al día siguiente |
| 403/429 (rate-limit GitHub) | Silencio; no marca `LastUpdateCheck` |
| JSON inválido / tag vacío | Silencio |
| Descarga interrumpida | `.part` eliminado; reintento posterior |
| SHA-256 no coincide | Instalador descartado y borrado |
| `artpicst-installer.exe` ausente en el release | Chequeo se considera correcto pero no hay acción |
| Instalación fallida a medias | Rollback automático desde los `.old` |
