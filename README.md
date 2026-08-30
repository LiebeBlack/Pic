# ARTPICST

Visor de imágenes nativo de alto rendimiento para Windows (C++ / Win32). Ventana con tema oscuro, renderizado de máxima fidelidad (GDI+, WIC, stb_image), orientación EXIF automática, soporte para transparencias y comprobador de actualizaciones integrado.

## Características Principales

- **Renderizado de Máxima Calidad**: Interpolación bicúbica de alta precisión con modo de envoltura clamp (sin bordes borrosos) y modo 1:1 pixel-perfect.
- **Fondo Ajedrezado Inteligente**: Visualización clara de transparencias en formatos PNG, WebP, ICO y GIF.
- **Orientación EXIF Automática**: Detecta la orientación de fotos tomadas con móviles o cámaras digitales y las orienta automáticamente.
- **Orden Natural de Archivos**: Clasificación numérica inteligente (`foto1`, `foto2`, `foto10`) utilizando la API nativa de Windows (`StrCmpLogicalW`).
- **Navegación y Zoom Avanzado**: Zoom centrado en el cursor, panorámica fluida, límites automáticos y prefetching en segundo plano con caché LRU.
- **Transformaciones Rápidas**: Rotación 90° (`R`), volteo horizontal (`H`) y volteo vertical (`V`).
- **Pase de Diapositivas**: Presentación automática con temporizador pulsando `F5`.
- **Integración con Windows**:
  - Enviar a la Papelera de reciclaje con `Supr` (`Delete`).
  - Mostrar y seleccionar la imagen en el Explorador de Windows con `Ctrl + E`.
  - Copiar imagen (`Ctrl + C`) o ruta completa (`Ctrl + Shift + C`) al portapapeles.
  - Registro de asociaciones de archivo con `--register` o mediante el instalador.
- **Updater Autónomo**: Comprobador de versiones en GitHub integrado (`Ctrl + U`), ejecutable con `dist\auto_updater.exe` sin dependencias externas.

## Formatos Soportados

1. **stb_image**: JPG, PNG, BMP, GIF, TGA, PSD, HDR, PIC, PNM/PPM/PGM.
2. **WIC (Windows Imaging Component)**: TIFF, ICO, WebP, HEIC/HEIF, AVIF, JPEG-XR/WDP y formatos RAW de cámaras (CR2, NEF, ARW, DNG, etc.).
3. **GDI+**: TIFF, EMF, WMF y códecs del sistema.

## Controles y Atajos de Teclado

| Acción | Atajo |
| --- | --- |
| Siguiente imagen | Flecha Derecha, Flecha Abajo, Espacio, botón ▶ |
| Imagen anterior | Flecha Izquierda, Flecha Arriba, Retroceso, botón ◀ |
| Primera / última | Inicio / Fin |
| Zoom | Rueda del ratón, `+` / `-` |
| Panorámica (Pan) | Clic izquierdo + arrastrar |
| Ajustar a ventana | `F` o botón *Ajustar* |
| Tamaño real (100%) | `1` o `0` |
| Rotar 90° | `R` (o `Shift + R` antihorario) |
| Volteo horizontal | `H` o botón *Voltear* |
| Volteo vertical | `V` |
| Modo presentación | `F5` |
| Eliminar a papelera | `Supr` (`Delete`) |
| Pantalla completa | `F11` o doble clic (salir con `ESC`) |
| Mostrar en Explorador | `Ctrl + E` |
| Copiar imagen | `Ctrl + C` |
| Copiar ruta | `Ctrl + Shift + C` |
| Abrir archivo | `Ctrl + O` |
| Abrir carpeta | `Ctrl + Shift + O` |
| Buscar actualizaciones | `Ctrl + U` |
| Información en barra | `I` (fija / automática) |
| Menú contextual | Clic derecho |
| Ayuda rápida | `F1` |

## Compilación y Empaquetado

### Visor ARTPICST (C++)
```bat
build.bat
```
El ejecutable se genera en `build\bin\artpicst.exe` (con CMake) o `dist\artpicst.exe`.

### Auto Updater (Python / PyInstaller)
```bat
python updater\build_exe.py
```
El ejecutable autónomo se genera en `dist\auto_updater.exe`.

## Uso por Línea de Comandos

```bat
artpicst.exe
artpicst.exe "C:\Fotos"
artpicst.exe "C:\Fotos\imagen.jpg"
artpicst.exe --maximized "C:\Fotos"
artpicst.exe --fullscreen "C:\Fotos"
artpicst.exe --register
artpicst.exe --unregister
```
