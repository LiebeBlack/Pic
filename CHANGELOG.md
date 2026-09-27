# Registro de cambios — ARTPICST

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
