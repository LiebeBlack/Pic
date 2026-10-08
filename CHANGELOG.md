# Registro de cambios — ARTPICST

## [Sin publicar]

### Baseline SIMD SSE4.2 (sin AVX) y cobertura real de los kernels
- **El núcleo de imagen pasa de SSE2 a SSE4.2.** El intercambio B↔R se resuelve
  con un solo `PSHUFB` (antes 2 AND + 2 desplazamientos + 2 OR); la réplica del
  alfa en el premultiplicado, con otro `PSHUFB` (antes 4 operaciones); la
  restitución del alfa original, con `PBLENDVB` (antes 2 AND + OR); y las dos
  detecciones de opacidad, con un único `PTEST` (antes `movemask` + comparación).
  La identidad de redondeo `(c·a + 127)/255` y la LUT de claridad se mantienen
  **bit a bit idénticas**.
- **AVX/AVX2/AVX-512 quedan prohibidos y vigilados.** `src/image_core.hpp` aborta
  en compilación si detecta `__AVX__`/`__AVX2__`/`__AVX512F__`, y el nuevo
  `tests/check_simd_policy.py` falla el build si algún fuente, script o workflow
  contiene un token capaz de activarlos. Los scripts de build y los tres
  workflows fijan `/arch:SSE4.2` (MSVC) o `-msse4.2` (MinGW) con sonda previa de
  toolchain: el build **no** se degrada a SSE2 en silencio.
- **Los kernels vectorizados no estaban cubiertos por los tests.** Los tests
  reservaban con `std::vector`, cuyas direcciones solo están garantizadas a 16 B:
  con `kPixelAlignment = 64 B` la comprobación `IsAligned(px, 64)` era falsa y solo
  se ejecutaba la cola escalar. El nuevo `TestSimdKernelsAligned` usa el asignador
  real, **verifica la alineación** y compara cada kernel contra su referencia
  escalar byte a byte.
- **Rendimiento medible:** nuevo `tests/kernel_bench.exe` (ns/píxel y GiB/s por
  kernel, con comparación SSE2 vs SSE4.2). Metodología en `docs/PERFORMANCE.md`.

### Calidad de origen y detalle de zoom
- **El límite de carga deja de estrangular el detalle.** El tope fijo de 3840 px
  reducía la foto *durante la carga*, así que el zoom al 100 % ya no mostraba los
  píxeles reales. Ahora se deriva de un presupuesto de memoria de 100 MB
  (lado máximo 5000 px, suelo 3840, techo 16384) y se puede ajustar con
  `ARTPICST_MAX_DIMENSION`. El valor efectivo se registra en el log.

### Diagnóstico, errores y validación
- **Logging real.** `LogMessage` pasa a un logger con niveles (INFO/WARN/ERROR),
  marca temporal, identificador de hilo, **rotación** a 1 MB y destino estable en
  `%LOCALAPPDATA%\ARTPICST` (antes `%TEMP%`, sin nivel ni límite). La escritura se
  serializa con un mutex porque el chequeo de actualizaciones corre en otro hilo.
- **Guardián de fallos:** `SetUnhandledExceptionFilter` + `std::set_terminate`
  registran la excepción y su dirección antes de terminar; antes un fallo no
  capturado cerraba la aplicación sin dejar rastro.
- **Todo error queda por escrito:** `HandleError` (punto único de los fallos de
  decodificación, E/S y portapapeles) registra a nivel ERROR; también se registran
  la indisponibilidad de Direct2D/DirectWrite, la pérdida del dispositivo GPU
  (con límite para no inundar el log) y los fallos de `SetTimer`.
- **Comprobación de versión de Windows** con `RtlGetVersion` (build real, no el
  falseado por el manifiesto) antes de usar atributos DWM 19/20/35/36/38.
- **Instalador:** verifica **SSE4.2 en la CPU** y Windows 10 (build 10240) antes
  de tocar el sistema, y comunica el estado de cada dependencia en la consola
  del wizard. Se añade `AppUserModelID` propio por rol (visor / instalador) para
  que la barra de tareas agrupe y muestre el icono correctos.

### Build, empaquetado y documentación
- **Enlace endurecido** en los tres binarios: `/GUARD:CF /DYNAMICBASE
  /HIGHENTROPYVA /NXCOMPAT`. No se usa `/fp:fast` (rompería la bit-exactitud).
- **Nuevo `Makefile`** (Git Bash / MSYS2 / MinGW-w64) con los objetivos `all`,
  `test`, `policy`, `bench`, `package`, `dist`, `msvc` y `clean`.
- **Nuevo `scripts/verify.ps1`**: un único comando que ejecuta todos los gates de
  calidad y sale con código distinto de cero si alguno falla.
- **CMake:** `ARTPICST_PACKAGE` (por defecto OFF) elimina el `FATAL_ERROR` que
  impedía cualquier `cmake --build` en limpio sin payload, y se añaden por fin los
  objetivos de test, el gate de política SIMD y el banco de kernels.
- **Documentación nueva:** `docs/PERFORMANCE.md` (baseline SIMD, kernels,
  metodología de medición, presupuesto de memoria) y `docs/INSTALL.md`
  (instalador, rutas, accesos directos, modos silenciosos, códigos de salida,
  dependencias y desinstalación limpia).

### Zoom ultra suave, libre y optimizado
- **Motor de zoom logarítmico**: la animación interpola `ln(zoom)` (progresión
  geométrica) en lugar del factor lineal: el tempo perceptual es idéntico a
  cualquier escala (0.5%→1% se siente como 100%→200%) y encadenar muescas de
  rueda compone sobre el objetivo en curso sin acelerones ni "muelles". El
  pivote queda clavado bajo el cursor en cada paso, con reanclaje sin deriva
  a mitad de animación. Duración adaptativa 90–140 ms según el salto.
- **Zoom libre sin centrado ni límites prácticos**: se eliminó el clamp y el
  recentrado automáticos (`EnsureImageVisible` ya no encierra la imagen; si
  queda fuera de la ventana, `F` / doble clic / botón "Ajustar" la recuperan)
  y el auto-snap a 100% durante la rueda (el 1:1 exacto sigue en tecla `1`/`0`,
  botón "1:1" y menú). Rango ampliado de 0.01x–200x a **0.005x–512x**.
- **Región sucia del zoom (GPU y GDI+)**: cada paso de la animación invalida
  solo la unión del rectángulo de pantalla que ocupa la imagen antes y después
  de moverse (+2 px de margen), no la ventana completa ~120 veces/s; el `BitBlt`
  final es parcial en la ruta GDI+ y Direct2D rasteriza solo la franja de
  movimiento. Con fallback seguro a repintado completo.
- **Caché de recursos del dock/OSD (ruta GDI+)**: los ~15 objetos GDI+ que se
  creaban y destruían por frame (paths del dock/sombra/11 botones, pinceles,
  penes, degradado activo) y la medición del texto del OSD ahora se cachean y
  solo se recrean al cambiar tema, geometría o mensaje. Mismo resultado visual.
- **Caché de layout del dock**: `LayoutHud` solo recalcula cuando cambia el
  tamaño del cliente, el modo pantalla completa o la etiqueta dinámica; antes
  se reconstruía en cada frame y en cada movimiento de ratón (vía `HitTestHud`).
- **Caché de `ImageAttributes`**: la matriz de color de efectos (grises /
  negativo / claridad) se construye una vez por efecto activo, no en cada frame.
- El zoom por teclado (↑/↓, +/−) y menú pivota sobre el **centro de la ventana**
  (punto fijo de referencia, no un recentrado).
- Corrección de compilación (MSVC 18, error C2660): `new (std::nothrow)` no
  compila sobre tipos GDI+ porque `GdiplusBase::operator new` no expone la
  forma alineada; el `ImageAttributes` cacheado se construye ahora con `::new`
  de colocación sobre almacenamiento `alignas` propio y se destruye con llamada
  explícita al destructor (mismo orden seguro respecto a `GdiplusShutdown`).

## [1.2.1] — 2026-09-27

Pase de auditoría completa: corrección de 7 defectos de seguridad/corrección,
tests y fuzzing automatizados, warnings estrictos y gates de CI.

### Seguridad (críticos)
- **EXIF**: lectura fuera de límites con JPEGs manipulados — el offset del IFD
  se comparaba en `uint32_t` y un valor cercano a `0xFFFFFFFF` desbordaba y
  pasaba el chequeo. Ahora se compara en 64 bits y el valor leído se sanea al
  rango 1..8.
- **Updater**: la verificación SHA-256 era *fail-open* — si el hash no podía
  calcularse se consideraba superada y se ejecutaba el instalador sin
  verificar. Ahora es *fail-closed*: hash no calculable ⇒ descarte.
- **Parser JSON**: escape `\u` desreferenciaba más allá del fin de cadena con
  respuestas truncadas; y una cadena sin cierre provocaba bucle infinito.

### Corrección
- `DecodeWithWic` liberaba un buffer de `AlignedPixelAlloc` con `free()`
  (corrupción de heap) en la ruta de error.
- La exportación guardaba al **tamaño de pantalla** (una foto 6000×4000 se
  convertía en 1920×1080); ahora exporta a la resolución de la imagen
  transformada y valida el códec antes de guardar.
- Updater `--background` dormía 1,5 s y mataba la descarga a medias; ahora
  espera al hilo con timeout de 10 minutos.
- El guard de 60 s sin datos nunca disparaba (se actualizaba la marca antes
  de comparar).
- `SetAsWallpaper` fallaba en silencio con formatos no soportados por Windows.
- Render GPU con DPI fijo 96 pese al manifiesto PerMonitorV2 (texto/OSD
  borrosos al 125/150 %); ahora usa el DPI real del monitor y recrea el target
  al arrastrar entre monitores.
- Instalador desatendido: `--silent`/`--dir` con exit codes documentados,
  validación del payload antes de tocar el sistema y desinstalación silenciosa
  que resuelve la carpeta registrada.

### Añadido
- Menú "Buscar actualizaciones" con atajo **Ctrl+U**; chequeo diario que
  respeta el checkbox de auto-instalación del updater.
- Módulos testeables: `installer/release_json.hpp` (parser compartido) y
  `src/jpeg_exif.hpp` (EXIF).
- Suite de tests: núcleo de imagen (9 suites + fuzz de allocator), EXIF
  (JPEGs sintéticos válidos/hostiles) y fuzzer JSON determinista (mutaciones,
  truncamientos exhaustivos, basura binaria).
- CI: gate de versión (version.json ↔ version.hpp ↔ recursos), tests en cada
  push y **fuzzing nocturno con AddressSanitizer**.
- Warnings estrictos en los tres binarios: `/W4 /permissive-` (MSVC) y
  `-Wall -Wextra` (MinGW).

### Modificado
- Menú contextual: estados activos con ✓ (Ultra-Claridad, B/N, Negativo,
  presentación, pantalla completa) en lugar de reescribir etiquetas.
- Diálogos con medición GDI+ real (sin recortes en textos largos) y tamaño
  limitado al área de trabajo.
- "Acerca de": C++23 + Direct2D, repo y licencia centralizados.
- Guía F1 sincronizada con los atajos reales (F1, Esc, Ctrl+Shift+A, Ctrl+U).

### Eliminado
- Código muerto: `CalculateOptimalTextSize`/`TextSizeInfo` (~90 líneas) y
  variables sin uso.

## [1.2.0] — 2026-08-30

Versión base del visor con instalador autocontenido y auto-actualizador.
