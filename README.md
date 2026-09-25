# ARTPICST

Visor de imágenes premium nativo para Windows (C++ / Win32) con interfaz acrílica moderna, renderizado de máxima fidelidad (GDI+, WIC, stb_image), orientación EXIF automática, soporte para transparencias y consumo ultra-ligero de recursos (RAM ≤ 80MB).

**[⬇ Descargar el instalador autocontenido](https://github.com/LiebeBlack/Pic/releases/latest/download/artpicst-installer.exe)** · [ZIP portable](https://github.com/LiebeBlack/Pic/releases/latest/download/artpicst-portable.zip) · [Web y documentación](docs/index.html) · [Arquitectura del auto-actualizador](docs/UPDATER.md)

## 🎨 Características Premium

- **Interfaz Glassmorphism/Acrílica**: Diseño moderno con efectos de transparencia, blur premium y colores vibrantes inspirados en Windows 11
- **Renderizado de Máxima Calidad**: Interpolación bicúbica de alta precisión con modo de envoltura clamp y modo 1:1 pixel-perfect
- **Ultra-Claridad HDR**: Modo de realce de detalles finos y micro-contraste
- **Zoom Extendido y Píxel Perfecto**: Auto-snap a 100% y ampliación ultra-nítida
- **Fondo Ajedrezado Inteligente**: Visualización clara de transparencias en PNG, WebP, ICO y GIF
- **Orientación EXIF Automática**: Detecta y corrige la orientación de fotos de móviles y cámaras
- **Navegación Avanzada**: Flechas ↑↓ para zoom, ←→ para imágenes, arrastrar para pan
- **Instalador Autocontenido**: Wizard GDI+ pitch-black con consola de log en vivo y payload incrustado (un solo `.exe` distribuible)
- **Auto-Actualizador Integrado**: Comprueba GitHub Releases una vez al día en segundo plano, notifica con una ventana flotante y actualiza con un clic (descarga verificada por SHA-256)
- **Consumo Ultra-Ligero**: Huella de memoria optimizada (RAM < 80 MB, CPU ~0%)

## 🚀 Formatos Soportados

1. **stb_image**: JPG, PNG, BMP, GIF, TGA, PSD, HDR, PIC, PNM/PPM/PGM
2. **WIC (Windows Imaging Component)**: TIFF, ICO, WebP, HEIC/HEIF, AVIF, JPEG-XR/WDP y RAW (CR2, NEF, ARW, DNG, etc.)
3. **GDI+**: TIFF, EMF, WMF y códecs del sistema

## ⌨️ Controles y Atajos

| Acción | Atajo |
| --- | --- |
| **Navegación** | |
| Siguiente imagen | →, Espacio, botón ▶ |
| Imagen anterior | ←, Retroceso, botón ◀ |
| Zoom in | ↑, Rueda arriba, `+` |
| Zoom out | ↓, Rueda abajo, `-` |
| Pausar / reproducir GIF | `P` |
| Primera / última | Inicio / Fin |
| **Visualización** | |
| Ajustar a ventana | `F` o botón *Ajustar* |
| Tamaño real (100%) | `1` o `0` |
| Pantalla completa | `F11` o doble clic (ESC para salir) |
| **Transformaciones** | |
| Rotar 90° | `R` (Shift+R antihorario) |
| Volteo horizontal | `H` |
| Volteo vertical | `V` |
| **Efectos** | |
| Ultra-Claridad HDR | `D` |
| Escala de grises | `G` |
| Invertir colores | `N` |
| **Sistema** | |
| Modo presentación | `F5` |
| Eliminar a papelera | `Supr` |
| Mostrar en Explorador | `Ctrl + E` |
| Copiar imagen | `Ctrl + C` |
| Copiar ruta | `Ctrl + Shift + C` |
| Abrir archivo | `Ctrl + O` |
| Abrir carpeta | `Ctrl + Shift + O` |
| **Otros** | |
| Panorámica (Pan) | Clic izquierdo + arrastrar |
| Menú contextual | Clic derecho |
| Ayuda | `F1` |

## 🔧 Compilación

### Opción 1: PowerShell (Recomendado)
```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```

### Opción 2: Batch
```cmd
build.bat
```

### Opción 3: MinGW (Alternativa)
```cmd
build_mingw.bat
```

## 📦 Archivos Generados

Después de la compilación exitosa en `dist\`:
- `artpicst.exe` - Programa principal premium (visor)
- `artpicst_updater.exe` - Módulo de auto-actualización
- `artpicst_installer.exe` - Instalador autocontenido (incrusta visor + updater + icono + README como recursos RCDATA)
- `artpicst.ico` - Icono de la aplicación
- `version.json` - Información de versión

> El **asset oficial de release** es `artpicst-installer.exe` (el instalador
> autocontenido renombrado): es el archivo exacto que el auto-actualizador
> descarga e instala. El workflow de CI (`.github/workflows/release.yml`) lo
> publica junto a `artpicst-portable.zip`.

## 💻 Uso por Línea de Comandos

```bat
artpicst.exe
artpicst.exe "C:\Fotos"
artpicst.exe "C:\Fotos\imagen.jpg"
artpicst.exe --maximized "C:\Fotos"
artpicst.exe --fullscreen "C:\Fotos"
artpicst.exe --register
artpicst.exe --unregister
```

El módulo de actualización también se puede usar directamente:

```bat
artpicst_updater.exe --check          # buscar actualizaciones (interactivo)
artpicst_updater.exe --forced         # forzar comprobación ignorando el límite diario
artpicst_updater.exe --background     # chequeo silencioso (1 vez al día)
artpicst_updater.exe --selftest       # autotest (parser JSON, SHA-256, comparador de versiones)
```

## 🔄 Auto-Actualización

1. El visor lanza `artpicst_updater.exe --background` como máximo una vez al día (hilo en segundo plano, la UI nunca se bloquea).
2. El updater consulta `https://api.github.com/repos/LiebeBlack/Pic/releases/latest` y compara la versión local con la publicada (soporta prefijos `v`, sufijos y números de más de un dígito).
3. Si hay versión nueva, muestra una notificación flotante minimalista con **Instalar actualización ahora** / **Recordar más tarde** y opción de instalar automáticamente futuras versiones.
4. Al aceptar: descarga `artpicst-installer.exe` con progreso real, verifica su hash SHA-256 y lo ejecuta con `--update <payload> --dir <dir>`.
5. El instalador sustituye los archivos, cierra el visor si sigue abierto y lo reinicia.

Sin conexión, con rate-limit de la API o si el servidor falla, el updater permanece en silencio: nunca interrumpe el uso del visor.

## 🏗️ Arquitectura

- **Main Program**: C++23 nativo con Win32 API, GDI+, WIC, stb_image (ultra-optimizado)
- **Installer** (`installer/`): wizard GDI+ autocontenido; extrae su payload RCDATA, instala con elevación UAC, rollback y modo silencioso `--update`; la instalación muestra 10 fases técnicas con consola de log en vivo
- **Updater** (`updater/`): módulo independiente con WinHTTP que consulta `releases/latest` en GitHub, compara versiones, verifica SHA-256 y encadena la instalación
- **Viewer ↔ Updater** (`src/updater_client.hpp`): chequeo diario en segundo plano y menú "Buscar actualizaciones..."

## 📋 Requisitos

- Windows 10/11
- Visual Studio 2019/2022 con C++ (o MinGW)
- Git (para clonar el repositorio)

## 📄 Licencia

Este software es proporcionado tal cual, sin garantía de ningún tipo.

## 🤝 Contribuciones

Las contribuciones son bienvenidas. Por favor:
1. Fork el repositorio
2. Crea una rama para tu feature
3. Commit tus cambios
4. Push a la rama
5. Abre un Pull Request

## 📞 Soporte

Para problemas o sugerencias, abre un issue en GitHub.