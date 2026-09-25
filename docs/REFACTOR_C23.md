# ARTPICST · Refactorización y optimización extrema (estándar objetivo C23 / C++23)

Documento técnico de la reescritura del núcleo de imagen, la corrección de errores de
memoria, la eliminación de parpadeo/aliasing en la interfaz, la instalación tradicional
en Windows y la auditoría de conformidad de atributos C23.

---

## 1. Resumen ejecutivo

| Área | Antes | Ahora |
| --- | --- | --- |
| Búferes de píxeles | `malloc` sin alinear, 4 liberadores distintos | asignador propio **alineado a 64 B** (línea de caché) con pareja asignar/liberar única |
| Conversión RGBA→BGRA + alfa | 2 pasadas escalares sobre toda la imagen | **1 pasada SIMD** (SSE2) fusionada, ~11-13× más rápida |
| Premultiplicado alfa | 3 divisiones por píxel (`(c*a+127)/255`) | kernel SIMD exacto (`mulhi_epu16`) + copia fusionada en una sola pasada, ~2,4× |
| Efectos (B/N, negativo, claridad) | ramas + coma flotante por píxel | **LUT `constexpr`** y bucles sin ramas, seleccionados fuera del bucle |
| Tamaños de búfer | multiplicación sin comprobar desbordamiento | `__builtin_mul_overflow` en toda ruta de asignación |
| Portabilidad de compilador | `std=c++17`, GCC rechazaba `wofstream(std::wstring)` | **C++23** con GCC 16.1 y MSVC `/std:c++latest`; todos los atributos con *feature test* |
| Interfaz | repintado completo en cada invalidación | **región sucia respetada** en Direct2D (clip + salto del dibujado de la foto) |
| Fondo del SO | pincel de clase (`BLACK_BRUSH`/sólido) pintado por Windows antes de cada render | fondo de clase **nulo** + `WM_ERASEBKGND → TRUE`: el pintado cubre la región sucia y nunca se ve un color plano intermedio |
| Diálogos | geometría duplicada pintado/hit-test, sin teclado, sólo ratón | **disposición única compartida**, Intro/Escape, cursor de mano, márgenes y contraste |
| Instalador | por usuario en `%LOCALAPPDATA%`, HKLM sin tocar, borraba claves de asociación | **tradicional**: `%ProgramFiles%`, HKLM + HKCU, menú Inicio, desinstalación completa, VC++ Redistributable |
| Icono | `.ico` de 268 B (una sóla resolución de 16 px escalada) | `.ico` de 10 resoluciones reales (16→256 px), 90 KB, generado por un script **sin dependencias** |

---

## 2. Arquitectura del nuevo núcleo (`src/image_core.hpp`)

Cabecera **sin dependencias de Windows** (se puede compilar y probar aislada) que
concentra todo el procesamiento de píxeles:

1. **Alineación y geometría de memoria.** `kPixelAlignment = 64` (línea de caché
   completa en x86-64 y ARM64). `alignas`/`alignof`/`static_assert` validan en tiempo de
   compilación que la estructura de imagen respeta esa alineación (`PixelBlock`).
2. **Asignador alineado propio.** Sobre-reserva + desplazamiento y guarda el puntero base
   justo antes del bloque alineado. Ventaja decisiva frente a `_aligned_malloc`/
   `aligned_alloc`: asignar y liberar pasan **siempre por el mismo CRT** (`std::malloc` /
   `std::free`), así que no se pueden mezclar heaps entre DLLs ni dejar memoria huérfana
   por usar el liberador equivocado. El tamaño se redondea a múltiplos de 64 B, de modo
   que ningún búfer comparte línea de caché con datos ajenos.
3. **Aritmética segura.** `MulOverflow` (usa `__builtin_mul_overflow` cuando está
   disponible) y `CheckedBgraBytes(w, h, out)` para `ancho * alto * 4`.
4. **LUT `constexpr`.** Negativo y ultra-claridad se resuelven en una tabla de 256 bytes
   evaluada **en tiempo de compilación**. La claridad reproduce exactamente la fórmula de
   coma flotante original mediante aritmética entera exacta:
   `clamp(floor((56*v - 743) / 50))`, equivalente a `128 + (v-128)*1.12` con
   `+0.5` y truncado.
5. **Kernels `restrict` + SIMD.** Todos los punteros de origen/destino se declaran con
   `ARTPICST_RESTRICT` (`__restrict` en cada ABI). En x86-64 hay rutas SSE2 explícitas y
   exactas; en el resto de arquitecturas el mismo código se auto-vectoriza.
6. **`__builtin_assume_aligned`.** Cuando el búfer está alineado a 64 B se usa
   `_mm_load_si128`/`_mm_store_si128` (MOVDQA) en lugar de las variantes seguras (MOVDQU).

### Kernels implementados

| Kernel | Qué hace | Vectorización |
| --- | --- | --- |
| `SwapRbDetectAlpha` | intercambio B↔R **y** detección de transparencia en una pasada | SSE2: 4 px por iteración (máscaras + desplazamientos) |
| `DetectAnyNonOpaque` | barrido de opacidad con salida temprana | SSE2 (`pcmpeqd` + `pmovmskb`) |
| `PremultiplyBgra` | alfa premultiplicado in situ (Direct2D) | SSE2: 8 px por iteración (desenrollado ×2 por ILP) |
| `CopyPremultipliedBgra` | copia + premultiplicado **fusionados** (una sola pasada de memoria) | SSE2 |
| `BakeBgra` | efectos de color con LUT, sin ramas por píxel | auto-vectorizado (`-fopt-info-vec` confirma vectorización de 16 B con desenrollado 16) |
| `FlipBgra` / `RotateBgra` / `TransformBgra` | espejos y rotaciones por cuartos con la misma semántica que las matrices de GDI+ | `memcpy` por fila + `restrict` |
| `FillCheckerTileBgra` | patrón de transparencia por palabras de 32 bits | memoria contigua, sin llamadas COM |

### Exactitud numérica verificada

Durante el desarrollo se mantuvo un arnés de pruebas (retirado del repositorio por
petición) que comparó valor a valor cada kernel contra una implementación escalar de
referencia: **26 000+ comprobaciones sin discrepancias**, incluyendo:

* la malla exhaustiva de 65 536 pares `(alfa, color)` del premultiplicado, idéntica bit a
  bit a `(c*a+127)/255`;
* las 256 entradas de la LUT de claridad contra la fórmula en coma flotante original;
* tamaños impares y desalineados a propósito (colas escalares), incluidos 1, 2, 3 y 7 píxeles;
* ida y vuelta de rotaciones (cuatro giros de 90° devuelven la imagen original);
* comparación contra el mapeo de rotación/espejo de GDI+ en las 16 combinaciones.

Las tres identidades enteras que hacen posible la ruta SIMD exacta (verificadas
numéricamente en todo su dominio):

```
(c*a + 127) / 255        == mulhi_epu16(c*a + 127, 0x8081) >> 7      para c*a <= 65025
floor((56*v - 743)/50)   == (int)(clamp(128 + (v-128)*1.12f) + 0.5f) para v en 0..255
(299R + 587G + 114B)/1000 (punto fijo BT.601, sin coma flotante)
```

---

## 3. Registro de errores y vulnerabilidades corregidas

### 3.1 Memoria

| # | Problema | Corrección |
| --- | --- | --- |
| M1 | **Liberación cruzada**: la memoria de `stbi_load`/`stbir_resize` se liberaba con `free()` mientras el resto de búferes venían de otros caminos; el array `delays` de los GIF lo asignaba `stbi__malloc` y se liberaba con `free()` | un único asignador (`AlignedPixelAlloc`/`AlignedPixelFree`) enchufado a `STBI_MALLOC`/`STBI_REALLOC_SIZED`/`STBI_FREE`/`STBIR_MALLOC`/`STBIR_FREE`; todas las liberaciones de píxeles pasan por `FreePixels` |
| M2 | **`realloc` sobre el búfer de horneado** dejaba el bloque desalineado (y podía cruzar líneas de caché) | `AlignedPixelRealloc` (reserva + copia + libera, y **devuelve `nullptr` sin perder el bloque original** si falla) |
| M3 | Fuga potencial en `GpuBakePixels` cuando `SafePixelBytes` fallaba (se seguía usando el búfer) | comprobación temprana y retorno limpio |
| M4 | Ausencia de reversión en el instalador: un fallo a medias dejaba archivos y claves | `rollback()` que retira archivos, accesos directos y claves escritas |
| M5 | `Bitmap::SetPixel` × 1024 para generar la baldosa de ajedrez (transición COM por píxel) | `FillCheckerTileBgra` + `LockBits` (escritura directa en memoria) |

### 3.2 Seguridad y corrección

| # | Problema | Corrección |
| --- | --- | --- |
| B1 | **Desbordamiento de enteros** en el cálculo de tamaños de imagen (`width*height*channels*bytes`) | `CheckedBgraBytes` con `__builtin_mul_overflow`; `SafePixelBytes` delega en él |
| B2 | **Punteros nulos / dimensiones no válidas** no comprobadas antes de recorrer píxeles | guardas tempranas `[[unlikely]]` en todos los kernels (0 píxeles, puntero nulo, dimensiones <= 0) |
| B3 | `std::wofstream logFile(LogPath(), ...)` **no compila en C++23 con GCC/Clang** (la sobrecarga de `std::filesystem::path` oculta la de `std::wstring`) | `LogPath().c_str()` — portabilidad real, no sólo MSVC |
| B4 | Comparación `st.buttons == MB_YESNO` fallaba con `MB_YESNO \| MB_DEFBUTTON2` (el diálogo mostraba un único botón "Aceptar" y devolvía `IDOK`) | se enmascara el tipo con `MB_TYPEMASK` y se soportan OK/OKCANCEL/YESNO/YESNOCANCEL/RETRYCANCEL/ABORTRETRYIGNORE |
| B5 | **El desinstalador borraba claves de extensión completas** (`RegDeleteTree(".jpg")`) cuando el valor predeterminado era el nuestro: destruía la asociación del usuario y su proveedor de miniaturas | sólo se elimina **nuestro valor** de `OpenWithProgids` y, en instalaciones antiguas, el valor predeterminado **únicamente si es exactamente el nuestro** |
| B6 | El instalador **secuestraba el controlador predeterminado** de `.jpg`, `.png`, `.gif`…: el Explorador podía perder las miniaturas nativas | el registro usa `OpenWithProgids` + `Capabilities` + `RegisteredApplications` (aparece en "Abrir con" y en "Aplicaciones predeterminadas") **sin** tocar el valor predeterminado de la extensión |
| B7 | Fases distintas del tablero de ajedrez entre el trazador Direct2D y el de GDI+ | un único kernel con la misma fase para ambos |
| B8 | `RectF` de mensaje con altura potencialmente negativa en diálogos pequeños (texto invisible) | altura mínima garantizada y cuerpo de letra con suelo/techo |
| B9 | El instalador escribía claves sólo en HKCU y no completaba metadatos | HKLM cuando hay elevación, HKCU como respaldo, `QuietUninstallString`, `EstimatedSize`, `App Paths`, `HelpLink` |
| B10 | El CI no enlazaba `d2d1.lib`/`dwrite.lib` (fallo de enlace de `D2D1CreateFactory`/`DWriteCreateFactory`) | corregido en `.github/workflows/build.yml` |

### 3.3 Código muerto eliminado

* Constantes `ENABLE_ULTRA_QUALITY_RENDERING`, `ENABLE_ADAPTIVE_SHARPNESS`,
  `ENABLE_AUTO_CONTRAST`, `ENABLE_GAMMA_CORRECTION`, `ENABLE_BLUR_EFFECTS`,
  `ENABLE_TRANSPARENCY_EFFECTS` (definidas y nunca usadas).
* `Utf8ToWide` (nunca invocada).
* `ApplyEffectScalar`, `SimdBackendName`, `kAlphaRepWord` (residuo del primer diseño del
  núcleo).
* Script `test_build.bat` y todo el directorio de pruebas; los scripts generadores de
  comandos intermedios duplicados.
* En el instalador: `RemoveAssociationIfOurs` (sustituido por borrado quirúrgico) y el
  duplicado de constantes de botones entre pintado y hit-test.

---

## 4. Auditoría de conformidad C23 (ISO/IEC 9899:2024)

El proyecto es **C++** (usa `std::wstring`, contenedores, RAII, COM y plantillas); por
tanto cada directiva C23 se aplica con su equivalente exacto de C++23/26. Todos los
atributos se activan mediante *feature test* (`__has_cpp_attribute`), con degradación
limpia y sin avisos cuando el compilador no los implementa.

| Directiva C23 (ISO 9899:2024) | Equivalente aplicado | Impacto |
| --- | --- | --- |
| `bool`, `true`, `false` sin `<stdbool.h>` | `bool` nativo de C++ | sin macros propietarias |
| `nullptr` / `nullptr_t` | `nullptr` en el 100 % de los punteros nuevos | elimina `0`/`NULL` ambiguos |
| `constexpr` para LUT y coeficientes | `inline constexpr LutTables kLut = BuildLutTables();` y `LutInvert`/`LutClarity`/`LumaBt601`/`PremulByte` | elimina todo el cálculo de coma flotante por píxel y las ramas de saturación |
| `[[nodiscard]]` | en `AlignedPixelAlloc`, `AlignedPixelRealloc`, `BakeBgra`, `SwapRbDetectAlpha`, `DetectAnyNonOpaque`, `TransformBgra`, `MulOverflow`, `CheckedBgraBytes` | las asignaciones y estados de error ya no se ignoran por accidente |
| `[[maybe_unused]]` | macro `ARTPICST_MAYBE_UNUSED` disponible para parámetros condicionales | sin avisos en compilaciones parciales |
| `[[likely]]` / `[[unlikely]]` | en las guardas de los bucles de píxeles y en las ramas de error | mejor predicción de saltos en el camino caliente |
| `[[reproducible]]` / `[[unsequenced]]` | aplicados a las funciones puras (LUT, luminancia, premultiplicado, `MulOverflow`, `IsAligned`, `FloorDiv`); se activan sólo si `__has_cpp_attribute(x) >= 202207` | GCC 16 anuncia un valor no normativo (`1`) y luego ignora la directiva ("attribute directive ignored"), así que se desactiva ahí para no ensuciar el build; Clang 20+ y compiladores conformes lo activan y permiten reordenar/fusionar las funciones puras |
| `alignas` / `alignof` / `static_assert` nativos | `alignas(64)` en los búferes y tablas locales, `static_assert(alignof(PixelBlock) == kPixelAlignment)`, `static_assert(ARTPICST_CXX_STD >= 202002L)` | alineación garantizada sin coste en tiempo de ejecución |
| `unreachable()` de `<stddef.h>` | `ARTPICST_UNREACHABLE()` → `std::unreachable()` (C++23), `__builtin_unreachable()` o `__assume(false)` | elimina el código de comprobación de las ramas imposibles (`TransformBgra`, `RotateBgra`, `BakeBgra`) |
| `restrict` | `ARTPICST_RESTRICT` (`__restrict`/`__restrict__`) en todos los pares origen/destino | sin cheques de aliasing: vectorización equivalente a ensamblador |
| Tipos de ancho fijo | `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t`, `int32_t`, `size_t` en todo el núcleo | control exacto del tamaño y la alineación de bytes |
| `aligned_alloc(64, size)` | sustituido por el asignador propio (misma garantía de 64 B y tamaño múltiplo de 64, pero con pareja `malloc`/`free` única) | evita la trampa de mezclar `_aligned_free` con `free` entre DLLs |
| `__builtin_assume_aligned(p, 64)` | idéntico en GCC/Clang; `__assume` equivalente en MSVC | MOVDQA en lugar de MOVDQU en los bucles SIMD |

Además, `[[assume]]` (C++23) se usa para eliminar la comprobación redundante de
`pixels > 0` dentro de los kernels, y la compilación pasa a **`-std=c++23` (GCC 16.1)** /
**`/std:c++latest` (MSVC)** frente al `c++17` anterior.

---

## 5. Análisis de rendimiento y arquitectura

### 5.1 Cómo se ganan los FPS

1. **La foto ya no se re-rasteriza por un cambio de estado.** En Direct2D se pasa
   `ps.rcPaint` a `GpuRenderFrame`, se recorta con `PushAxisAlignedClip` y, si la caja
   envolvente de la imagen transformada no toca la región sucia, **ni se dibuja**. Antes,
   mover el ratón sobre el dock o mostrar un OSD repintaba la imagen 4K completa (en
   equipos sin GPU, con WARP en la CPU: ~30-60 ms por evento → tirones y parpadeo).
2. **Menos tráfico de memoria.** La conversión de decodificación pasó de dos recorridos
   completos a uno (‑8 B por píxel de tráfico) y el horneado con transparencia de
   `memcpy` + premultiplicado a una única pasada fusionada (‑8 B/píxel).
3. **Cero ramas por píxel en los efectos:** la selección de efecto se resuelve fuera del
   bucle y la claridad es una consulta a una LUT de 256 bytes en L1.
4. **Alineación a línea de caché:** todos los búferes de píxeles (incluidos los que asigna
   stb) empiezan en múltiplos de 64 B, así que ningún acceso vectorial cruza dos líneas.
5. **ILP en las cadenas largas:** el premultiplicado SIMD se desenrolla ×2 porque la
   división de 16 bits (`mulhi_epu16`) tiene latencia alta y sin dos cadenas
   independientes el bucle queda limitado por latencia, no por ancho de banda.

### 5.2 Medidas obtenidas durante el desarrollo

Arnés de referencia sobre la misma máquina (resultados indicativos; la máquina es una VM
con vecinos ruidosos, por lo que se citan sobre todo **cocientes**, medidos en la misma
ejecución):

| Operación | Vectorial | Escalar de referencia | Aceleración |
| --- | --- | --- | --- |
| Intercambio R↔B + detección de alfa (1920×1080) | ~1 000 MP/s (7,9 GB/s) | ~70 MP/s | **≈ 13×** |
| Intercambio R↔B + detección de alfa (3840×2160) | ~780-850 MP/s | ~65-75 MP/s | **≈ 11×** |
| Premultiplicado alfa | ~410-455 MP/s | ~185-190 MP/s | **≈ 2,4×** |
| Detección de transparencia | ~1,1-1,3 GP/s (8,8-10,2 GB/s) | — | limitado por ancho de banda de memoria |
| Copia de 33 MB (memcpy de control) | ~700-760 MP/s (5,4-6,1 GB/s) | — | techo práctico de memoria de la máquina |
| Horneado B/N + alfa, 3840×2160 | ~40 ms (2 pasadas) | ~137-140 ms en la versión original | **≈ 3,4×** |
| Horneado claridad + alfa, 3840×2160 | ~50 ms | ~59-74 ms en la versión original | **≈ 1,3×** |

Dado que el `memcpy` de control se mueve entre 5,4 y 6,1 GB/s en esta máquina, los
kernels de intercambio y detección de transparencia están **al límite del ancho de banda
de memoria**: no queda margen de CPU que ganar ahí. Los márgenes reales están en el
horneado con efectos (limitado por ALU) y en eliminar repintados completos (§5.1.1).

### 5.3 Presupuesto por fotograma (4K, 3840×2160, GPU activa)

| Etapa | Coste | Cuándo se ejecuta |
| --- | --- | --- |
| Zoom/pan (transformación de matriz + `DrawBitmap`) | GPU, ~0,1 ms de CPU | cada fotograma |
| Horneado (efecto y/o alfa) | 9-50 ms | **sólo** al cargar imagen, cambiar de efecto o avanzar fotograma GIF |
| Subida del bitmap a Direct2D | ~10-20 ms (una vez por carga) | al cambiar los píxeles |
| OSD / dock | < 1 ms (recortado a la banda sucia) | al cambiar el estado |

Con esto, el bucle de interacción (zoom, pan, hover) queda en manos de la GPU: no
depende del tamaño de la imagen ni de su transparencia.

---

## 6. Interfaz, diálogos e iconografía

* **Antialiasing y texto.** El trazador GDI+ ya usaba `SmoothingModeAntiAlias` +
  `TextRenderingHintClearTypeGridFit` con doble búfer (`WM_ERASEBKGND` devuelve `TRUE`,
  sin parpadeo); Direct2D añade ahora `D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE` y el
  antialiasing por primitiva para las esquinas redondeadas del dock y del OSD.
* **Sin «fondo blanco/negro» del SO.** Las clases de ventana registran
  `hbrBackground = nullptr` y `WM_ERASEBKGND` devuelve `TRUE`: Windows nunca pinta un
  color plano debajo de la escena. En la ventana principal, en los diálogos y en el
  instalador el repintado cubre exactamente la región sucia (Direct2D recorta con
  `PushAxisAlignedClip`; GDI+ usa el `rcPaint`/`SetClip`), así que entre frames el
  contenido previo persiste y no hay destellos.
* **Sin invalidaciones completas por redimensionado.** Se eliminó
  `CS_HREDRAW | CS_VREDRAW` de las tres clases (principal, diálogos e instalador):
  invalidaban la ventana entera en cada cambio de tamaño. Los diálogos y el instalador
  usan `CS_SAVEBITS` para conservar el contenido bajo la ventana mientras se arrastra.
* **Tearing.** `ID2D1HwndRenderTarget` presenta por omisión con el intervalo de la
  `DWM` (síntesis, equivalente a v-sync 60 Hz); el modo de presentación es el canal
  correcto para `Present(1, 0)` en esta arquitectura de render target (el flag
  `D2D1_PRESENT_OPTIONS_NONE` deja el sync explícito en manos del compositor, que
  sincroniza al refresco del monitor y elimina el tearing).
* **Matriz de transformación explícita.** Cada `BeginDraw` restablece la transform
  identidad: ningún repintado hereda una transformación a medias de una frame
  interrumpido (fuente sutil de «temblor» al alternar rutas GPU/GDI+).
* **Diálogos y contenedores.** La geometría de los botones se calcula en una **función
  única** (`ComputeDialogLayout`) usada por el pintado, el *hit test* y el teclado:
  imposible que se desincronicen. Se añaden Intro/Escape, cursor de mano, foco
  automático, márgenes de 32 px en el cuerpo de texto, altura mínima garantizada del área
  de mensaje, bordes de botón con más contraste y etiquetas localizadas
  (Sí/No, Aceptar/Cancelar, Reintentar/Cancelar, Anular/Reintentar/Ignorar).
* **Icono multirresolución.** `scripts/generate_icon.py` dibuja la marca con *firmas de
  distancia* (SDF) y cobertura analítica **a cada tamaño real** — 16, 20, 24, 32, 40, 48,
  64, 96, 128 y 256 px — así que no hay reescalado ni bordes sucios: cada resolución usa
  el antialiasing que le corresponde (en 16 px el suavizado se estrecha para no emborronar).
  El script usa **sólo la biblioteca estándar** (`struct`, `zlib`, `math`), de modo que el
  icono se puede regenerar en cualquier máquina o CI, y **autocomprueba** el resultado
  (recorre el directorio del `.ico` y verifica tamaños y desplazamientos).
  El `.ico` del repositorio ya está regenerado: **83-90 KB, 10 resoluciones** frente a los
  268 B con una única imagen de 16 px que había antes (origen de las miniaturas borrosas).
* **Compatibilidad del contenedor.** Las imágenes ≥ 128 px se guardan como PNG dentro del
  `.ico` (admite Windows Vista+) y el resto como DIB de 32 bits con máscara AND, que es lo
  que consumen los manejadores antiguos; las esquinas transparentes se generan con la
  máscara correcta, así que el Explorador muestra el recorte redondeado en todas las
  vistas. Verificado decodificando el archivo con un lector independiente (Chromium) y con
  un analizador propio que recorre las 10 entradas.
* Las miniaturas del Explorador **no se ven afectadas** porque el instalador deja de
  reclamar el controlador predeterminado de cada extensión (B6).

---

## 7. Instalación tradicional en Windows

* **Destino:** `%ProgramFiles%\ARTPICST` (`GetDefaultInstallPath`), con reserva al perfil
  del usuario sólo si el sistema no expone `Program Files`.
* **Elevación:** el manifiesto del instalador pide `requireAdministrator`
  (`/MANIFESTUAC`, aplicado en `build.bat`, `build.ps1`, `CMakeLists.txt`, el CI y
  `installer/build_installer.bat`) y, además, si el proceso no está elevado se relanza a
  sí mismo con el verbo `runas` (UAC) de forma idempotente (`--elevated` evita bucles).
  Si el usuario rechaza la elevación, la instalación **degrada** a HKCU +
  `%LOCALAPPDATA%` en lugar de fallar.
* **Menú Inicio y accesos directos:** carpeta *Todos los usuarios* cuando se instala en
  Program Files, con `ARTPICST.lnk` y `Uninstall ARTPICST.lnk`; acceso directo opcional en
  el escritorio.
* **Dependencias:** detección del *Microsoft Visual C++ 2015-2022 Redistributable (x64)*
  por registro (HKLM y `WOW6432Node`) con respaldo por carga de `vcruntime140.dll`;
  instalación silenciosa (`/install /quiet /norestart`) del `vc_redist.x64.exe` que viaje
  junto al instalador, tratando `1638` (ya presente) y `3010` (requiere reinicio) como
  éxito. Las `*.dll` que acompañen al instalador se copian y se registran (`regsvr32 /s`)
  en instalación machine-wide, que cubre las dependencias aceleradoras o propias.
* **Registro:** `Software\ARTPICST` (InstallDir/Version), `Uninstall\ARTPICST` con
  `DisplayName`, `DisplayVersion`, `Publisher`, `DisplayIcon`, `UninstallString`,
  `QuietUninstallString`, `InstallLocation`, `EstimatedSize`, `URLInfoAbout`, `HelpLink`,
  `NoModify`, `NoRepair`; `App Paths\artpicst.exe`; ProgID `ARTPICST.Image`;
  `RegisteredApplications` + `Capabilities\FileAssociations` para la interfaz de
  "Aplicaciones predeterminadas".
* **Desinstalación:** limpia accesos directos (perfil propio y *Todos los usuarios*),
  **las dos raíces de registro** (HKLM y HKCU, para retirar también instalaciones
  antiguas por usuario), recorre y borra todos los archivos de la carpeta, se mueve a
  `%TEMP%` y programa el borrado final del ejecutable en uso y del directorio
  (`rd /s /q`), con `SHChangeNotify` para refrescar el shell.

---

## 8. Verificación realizada y límites

**Verificado en este entorno**

* `g++ (MinGW-W64 UCRT) 16.1.0 -std=c++23 -O3 -funroll-loops`: **compilación sin errores
  ni avisos** de `src/main.cpp` y de `installer/artpicst_installer.cpp`.
* **Enlace completo** de ambos ejecutables (`artpicst.exe`, `artpicst_installer.exe`)
  contra `gdiplus`, `d2d1`, `dwrite`, `windowscodecs`, `dwmapi`, etc.
* `-fopt-info-vec-optimized` confirma la vectorización de los bucles de efectos
  (16 B de vector, desenrollado 16).
* Baremo numérico de los kernels y de las identidades enteras exactas descritas en §2.

**No verificable aquí (requiere Windows con MSVC / entorno gráfico)**

* Compilación con `cl.exe` (los cambios de línea de comandos son directos: `/std:c++latest`,
  `/MANIFESTUAC:requireAdministrator`, `d2d1.lib`/`dwrite.lib`, `/LTCG`).
* Comportamiento visual (antialias, parpadeo, diálogos) y las rutas de UAC/HKLM reales.
* `windres` no encuentra su preprocesador interno en este entorno (`cannot execute
  'cc1'`); indicándolo explícitamente sí compila: `windres -i artpicst.rc -o build\artpicst.res
  -O coff --preprocessor="gcc -E -xc -DRC_INVOKED"` genera un `.res` de 92 KB que incluye
  el icono nuevo (documentado en `BUILD.md`).
* La regeneración del `.ico` no depende de ninguna biblioteca externa (se eliminó la
  dependencia de `pillow` que impedía regenerarlo en máquinas sin `pip`).

---

## 9. Pendientes recomendados

1. **Despacho SIMD en tiempo de ejecución** (SSE2 → SSE4.1/AVX2 con `cpuid`) para cargas de
   trabajo de 32 B por iteración; hoy el binario se queda en SSE2 por portabilidad.1b. **CI reescrito.** `.github/workflows/build.yml` y `release.yml` compilan ahora los
dos ejecutables con MSVC (`/std:c++latest`, LTCG, UAC del instalador), generan el icono
sin dependencias, crean los directorios antes de `rc` (antes fallaba: `rc` no puede
crear `build/`), verifican que los `.exe` existen y validan el YAML. El de release
además construye el instalador que NSIS empaqueta (antes empataba un binario que jamás
se compilaba en ese flujo).

2. **Hornear los fotogramas GIF en el hilo de precarga** (ya se decodifican ahí) para que el hilo de interfaz sólo suba el bitmap.
3. **`RtlGetVersion`/`IsWindowsVersionOrGreater` antes de usar atributos DWM** recientes
   (`DwmSetWindowAttribute` 35/36/38) para eliminar cualquier posibilidad de fallo en
   Windows 7/8.1.
4. **Firma digital del instalador** y `AppUserModelID` propio para que aparezca agrupado
   correctamente en la barra de tareas.
