# ARTPICST - Visor de Imágenes Ultraligero Profesional

Visor de imágenes minimalista y ultraligero para Windows, inspirado en la velocidad y limpieza del antiguo Picasa. Ahora con características avanzadas de nivel profesional.

## Características Principales

### Rendimiento y Velocidad
- **Ultraligero:** Ejecutable nativo compilado con C++ y Win32 API, sin frameworks pesados
- **Caché LRU Inteligente:** Sistema de caché de hasta 5 imágenes para navegación instantánea sin tirones
- **Pre-carga en Segundo Plano:** Decodificación asíncrona con hilos para la siguiente y anterior imagen
- **Renderizado sin Parpadeos:** Doble búfer optimizado con GDI+ y calidad máxima

### Interfaz Profesional
- **Interfaz minimalista:** Ventana frameless con fondo oscuro neutro (#121212)
- **Panel OSD:** Visualización en pantalla con dimensiones, peso en disco y posición en la lista
- **Zoom Focal Preciso:** Zoom centrado exactamente en las coordenadas del cursor
- **Pan Suave:** Desplazamiento fluido con clic izquierdo y arrastrar

### Soporte de Formatos Completo
- **Formatos universales:** JPG, PNG, BMP, WebP, GIF, TGA, PSD, HDR, PIC, PNM, PPM, PGM
- **Carga rápida:** Decodificación optimizada con stb_image (header-only)

### Interactividad Avanzada
- **Navegación fluida:** Flechas, espacio para cambio instantáneo entre imágenes
- **Drag & Drop:** Arrastrar y soltar archivos o carpetas directamente en la ventana
- **Atajos de teclado:** Controles intuitivos para todas las funciones
- **Exploración automática:** Recorre automáticamente todas las imágenes de la carpeta

## Requisitos

- Windows 7 o superior
- Visual Studio 2017 o superior (o cualquier compilador C++17 compatible)
- CMake 3.10 o superior

## Compilación

### Con CMake (Recomendado)

```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

El ejecutable resultante estará en `build/Release/artpicst.exe`

### Compilación manual con Visual Studio

```bash
cl /EHsc /std:c++17 /O2 /Fe:artpicst.exe src/main.cpp /link gdiplus.lib user32.lib kernel32.lib /SUBSYSTEM:WINDOWS /ENTRY:WinMain
```

## Uso

### Ejecutar sin argumentos
El visor buscará imágenes en la carpeta donde se encuentra el ejecutable:

```bash
artpicst.exe
```

### Ejecutar con una carpeta específica
```bash
artpicst.exe "C:\Mis\Imágenes"
```

### Ejecutar con un archivo específico
El visor cargará el archivo y explorará la carpeta que lo contiene:

```bash
artpicst.exe "C:\Mis\Imágenes\foto.jpg"
```

## Controles

### Navegación
- **Flecha izquierda / Flecha arriba:** Imagen anterior
- **Flecha derecha / Flecha abajo / Espacio:** Imagen siguiente
- **ESC:** Cerrar aplicación

### Zoom y Visualización
- **Rueda del ratón:** Zoom in/out (zoom focal preciso hacia el cursor)
- **Clic izquierdo + arrastrar:** Pan (desplazar imagen)
- **Tecla F:** Ajustar imagen a ventana (fit to screen)
- **Tecla I:** Mostrar/ocultar panel OSD manualmente
- **Tecla R:** Rotar imagen 90 grados
- **Tecla F11:** Alternar modo pantalla completa (fullscreen)

### Interactividad
- **Drag & Drop:** Arrastrar archivos o carpetas a la ventana para abrirlos
- **Click en carpeta:** Explora automáticamente todas las imágenes contenidas
- **Ctrl + C:** Copiar imagen actual al portapapeles
- **Ctrl + V:** Copiar ruta de archivo actual al portapapeles

## Arquitectura Avanzada

### Core y Rendimiento
- **Core:** C++ puro con Win32 API y C++17
- **Threading:** std::thread para pre-carga asíncrona de imágenes
- **Sincronización:** std::mutex, std::condition_variable para gestión segura de hilos
- **Caché:** Implementación LRU (Least Recently Used) con std::map y std::list

### Renderizado y Calidad
- **Renderizado:** GDI+ con InterpolationModeHighQualityBicubic
- **Doble búfer:** Implementación optimizada para cero parpadeo
- **Calidad:** PixelOffsetModeHighQuality y SmoothingModeHighQuality
- **Transparencia:** AlphaBlend para OSD semitransparente

### Gestión de Memoria
- **LRU Cache:** Máximo 5 imágenes en memoria RAM (~50-100MB dependiendo del tamaño)
- **Prefetching:** Pre-carga inteligente de +2/-2 imágenes desde la actual
- **Header-only:** stb_image para zero-overhead en decodificación

### Optimizaciones de Compilación
- **MSVC:** /O2 /GL /Oi /Ot /Oy /GF para máximo rendimiento
- **Link-time:** /LTCG /OPT:REF /OPT:ICF para optimización de enlace
- **Tamaño:** /Os para tamaño mínimo del ejecutable

## Optimizaciones de Rendimiento

### Nivel de Aplicación
- **Caché LRU:** 5 imágenes en memoria para navegación instantánea
- **Prefetching:** Decodificación en segundo plano de imágenes adyacentes
- **Zero-copy:** Copia mínima de datos entre caché y visualización
- **Lazy loading:** Solo carga lo necesario según navegación del usuario

### Nivel de Renderizado
- **Doble búfer:** Evita parpadeos en WM_PAINT y WM_SIZE
- **WM_ERASEBKGND:** Override para eliminar flicker
- **GDI+ optimizado:** Configuración de máxima calidad y rendimiento
- **OSD eficiente:** Renderizado condicional y auto-ocultado

### Nivel de Compilación
- **Optimizaciones MSVC:** /O2 /GL /Oi /Ot /Oy /GF
- **Link-time code generation:** /LTCG /OPT:REF /OPT:ICF
- **Size optimization:** /Os para ejecutable minimal
- **Modern C++:** C++17 con threading y sincronización eficiente

## Hardening y Estabilidad

### Gestión de Memoria Robusta
- **RAII:** Destructores automáticos para liberación de recursos
- **Smart pointers:** Movimiento semántico para evitar copias de punteros
- **Validación de punteros:** Chequeos de null antes de uso
- **Limpieza garantizada:** Liberación segura en destructor de AppState
- **Fugas de memoria:** Auditoría completa de GDI+, Bitmaps y DCs

### Concurrencia Thread-Safe
- **Mutex protectors:** std::mutex para caché LRU y operaciones compartidas
- **Atomic operations:** std::atomic para flags de control de hilos
- **Condition variables:** std::condition_variable con timeout para evitar deadlocks
- **Race condition prevention:** Sincronización explícita en todas las operaciones críticas
- **Graceful shutdown:** Terminación segura de hilos con timeout

### Manejo de Errores Profesional
- **Validación de archivos:** Chequeo de integridad antes de decodificación
- **Excepciones C++:** Try-catch blocks en todas las operaciones críticas
- **Fallback automático:** Recuperación ante archivos corruptos o memoria insuficiente
- **Graceful degradation:** Reducción de funcionalidad ante errores recurrentes
- **Error logging:** Sistema de logging en modo debug para diagnóstico

### Validación de Integridad
- **Límites de tamaño:** Máximo 500MB por archivo para evitar DOS
- **Dimensiones máximas:** Límite de 20K pixels por dimensión
- **Validación stb_image:** Chequeo de errores de decodificación
- **Verificación de accesibilidad:** Comprobación de permisos de archivo
- **Sanitización de rutas:** Validación de paths y conversión segura UTF-8

## Características de Seguridad y Estabilidad

### Protección contra Crashes
- **Validación de entradas:** Todos los parámetros validados antes de uso
- **Bounds checking:** Verificación de límites en arrays y buffers
- **Safe string handling:** Funciones seguras de Windows (wcscpy_s, swprintf_s)
- **Exception safety:** Todas las operaciones críticas protegidas
- **Recovery automática:** El programa continúa funcionando tras errores no críticos

### Protección contra Memory Leaks
- **Auditoría de recursos:** Seguimiento exhaustivo de todos los handles y punteros
- **Cleanup garantizado:** Liberación en destructores y en WM_DESTROY
- **Validación de GDI+:** Verificación de objetos Graphics y Bitmaps
- **Thread cleanup:** Terminación y limpieza segura de hilos secundarios
- **Cache management:** LRU automático para evitar acumulación infinita

### Modo Degradado Graceful
- **Reducción automática:** Disminución de funcionalidad ante errores recurrentes
- **Caché reducida:** De 5 a 2 imágenes en modo degradado
- **Prefetch desactivado:** Desactivación de pre-carga si hay problemas
- **Recuperación automática:** Reactivación gradual tras estabilización
- **Usuario informado:** Mensajes claros sobre el estado del sistema

## Tamaño objetivo

El ejecutable compilado en Release debería estar por debajo de 500 KB gracias a:
- No usar frameworks pesados (Qt, Electron, MFC)
- Librerías header-only
- Optimizaciones de compilación
- Enlace estático mínimo
- Eliminación de código muerto mediante LTCG

## Licencia

Este proyecto utiliza:
- stb_image.h - Dominio público (public domain)
- stb_image_resize2.h - Dominio público (public domain)

El código principal del proyecto se puede distribuir bajo la licencia que el desarrollador determine.