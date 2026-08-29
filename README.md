# ARTPICST

Visor de imágenes nativo para Windows (C++ / Win32). Ventana real del sistema, barra oscura y carga con varios motores.

## Qué hace

- Ventana estándar de Windows (minimizar, maximizar, ajuste a bordes, DPI)
- Barra inferior con anterior/siguiente, ajustar, 100%, rotar, pantalla completa y abrir
- Zoom al cursor, pan, rotación y pantalla completa (F11)
- El ajuste a ventana se mantiene al redimensionar; el zoom manual no se pierde
- Caché LRU + precarga de las imágenes vecinas
- Arrastrar y soltar archivos o carpetas
- Menú contextual (clic derecho)

## Formatos (con fallback)

El visor prueba **tres decodificadores**, en este orden:

1. **stb_image** — JPG, PNG, BMP, GIF, TGA, PSD, HDR, PIC, PNM/PPM/PGM
2. **WIC** (Windows Imaging Component) — TIFF, ICO, JPEG-XR/WDP, y también WebP, HEIC/HEIF, AVIF, RAW (CR2, NEF, ARW, DNG…) si Windows tiene el códec instalado
3. **GDI+** — TIFF, EMF, WMF y otros que el sistema sepa abrir

Si un motor no puede, pasa al siguiente. En la barra se muestra qué motor abrió el archivo (`stb`, `WIC` o `GDI+`).

## Requisitos

- Windows 7 o superior (WebP/HEIC/AVIF dependen de los códecs del sistema, típicos en Windows 10/11)
- Para compilar: CMake 3.16+ y Visual Studio 2017+ (o Build Tools con C++)

## Compilación

```bat
build.bat
```

El ejecutable queda en `build\bin\artpicst.exe` (CMake) o `build\artpicst.exe` (cl.exe).

## Uso

```bat
artpicst.exe
artpicst.exe "C:\Fotos"
artpicst.exe "C:\Fotos\imagen.webp"
artpicst.exe --maximized "C:\Fotos"
artpicst.exe --fullscreen
```

Sin argumentos abre una ventana vacía lista para arrastrar o usar **Ctrl+O**.

## Controles

| Acción | Atajo |
| --- | --- |
| Anterior / siguiente | Flechas, Espacio, Retroceso, botones ◀ ▶ |
| Primera / última | Inicio / Fin |
| Zoom | Rueda del ratón, `+` / `-` |
| Pan | Clic izquierdo + arrastrar |
| Ajustar a ventana | `F` |
| Tamaño real (100%) | `0` o `1` |
| Rotar 90° | `R` |
| Pantalla completa | `F11` o doble clic. `ESC` vuelve a ventana |
| Cerrar | `ESC` (en ventana) o el botón de cerrar |
| Abrir archivo | `Ctrl+O` |
| Abrir carpeta | `Ctrl+Shift+O` |
| Copiar imagen | `Ctrl+C` |
| Copiar ruta | `Ctrl+Shift+C` |
| Info en barra | `I` fija o auto; `F1` muestra atajos |
| Menú | Clic derecho |
