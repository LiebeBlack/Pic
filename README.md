# ARTPICST

Visor de imágenes premium nativo para Windows (C++ / Win32) con interfaz acrílica moderna, renderizado de máxima fidelidad (GDI+, WIC, stb_image), orientación EXIF automática, soporte para transparencias y consumo ultra-ligero de recursos (RAM ≤ 80MB).

## 🎨 Características Premium

- **Interfaz Glassmorphism/Acrílica**: Diseño moderno con efectos de transparencia, blur premium y colores vibrantes inspirados en Windows 11
- **Renderizado de Máxima Calidad**: Interpolación bicúbica de alta precisión con modo de envoltura clamp y modo 1:1 pixel-perfect
- **Ultra-Claridad HDR**: Modo de realce de detalles finos y micro-contraste
- **Zoom Extendido y Píxel Perfecto**: Auto-snap a 100% y ampliación ultra-nítida
- **Fondo Ajedrezado Inteligente**: Visualización clara de transparencias en PNG, WebP, ICO y GIF
- **Orientación EXIF Automática**: Detecta y corrige la orientación de fotos de móviles y cámaras
- **Navegación Avanzada**: Flechas ↑↓ para zoom, ←→ para imágenes, arrastrar para pan
- **Instalador Premium**: Interfaz gráfica moderna con asistente paso a paso
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
- `artpicst.exe` - Programa principal premium
- `artpicst_installer.exe` - Instalador con interfaz moderna
- `artpicst.ico` - Icono de la aplicación
- `version.json` - Información de versión

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

## 🏗️ Arquitectura

- **Main Program**: C++ nativo con Win32 API, GDI+, WIC, stb_image (ultra-optimizado)
- **Installer**: C++ con GDI+ para interfaz gráfica premium

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