# ARTPICST — Rendimiento del núcleo de imagen

Este documento describe el baseline SIMD del proyecto, los kernels que lo
implementan, cómo medirlos y qué cifras hay registradas. **Sustituye a cualquier
afirmación de rendimiento que no venga acompañada de una medición reproducible.**

> **Estado de las mediciones: PENDIENTES.** Las cifras de la sección 5 no se han
> rellenado porque el entorno donde se aplicó esta auditoría **no dispone de
> toolchain** (sin `cl.exe`, `g++`, `cmake`, `rc` ni Windows SDK), así que no fue
> posible compilar ni ejecutar el banco. Ejecuta `make bench` (o
> `scripts\verify.ps1 -Bench`) en una máquina con Visual Studio 2022 y pega los
> resultados: la tabla está preparada para ello. **No se ha inventado ninguna
> cifra.**

---

## 1. Política SIMD: baseline SSE4.2, AVX prohibido

| Aspecto | Decisión |
| --- | --- |
| Baseline | **SSE4.2** (incluye SSE2/SSSE3/SSE4.1) |
| Prohibido | AVX, AVX2, AVX-512, FMA, cualquier ensanchado a 256/512 bits |
| Ancho de registro | 128 bits (`kSimdAlignment = 16`) |
| Alineación de píxel | 64 B (`kPixelAlignment`, línea de caché completa) |
| Flag MSVC | `/arch:SSE4.2` + `/DARTPICST_TARGET_SSE42=1` |
| Flag GCC/Clang | `-msse4.2` + `-DARTPICST_TARGET_SSE42=1` |
| CPU mínima | Intel Nehalem / AMD Bulldozer (2008+) |

**Por qué no AVX.** Estos kernels están limitados por ancho de banda de memoria,
no por ALU: cada byte de la imagen se lee y se escribe al menos una vez. Ensanchar
los registros de 128 a 256 bits no aumenta el tráfico útil, pero sí exige CPUs de
2011+, penaliza en downclocking y complica el despliegue. Cuando hace falta más
paralelismo se ensancha **por número de píxeles** (más cargas de 128 bits en
vuelo para llenar el pipeline), que es lo que hacen los bucles desenrollados ×2.

**Por qué no hay despacho en tiempo de ejecución.** Con el baseline fijado en
compilación, el código vectorizado es el único código: cero ramas indirectas por
llamada, cero tablas de punteros y una sola ruta que testear. El coste es el
requisito de CPU (arriba), que es razonable para un visor de Windows 10/11.

**Guardias automáticas.** `tests/check_simd_policy.py` recorre fuentes, scripts y
workflows y **falla el build** si aparece cualquier token capaz de activar AVX
(`__m256*`, `_mm256_*`, `/arch:AVX`, `-mavx`, `-march=native`...). Además,
`src/image_core.hpp` aborta en compilación con `#error` si detecta `__AVX__`,
`__AVX2__` o `__AVX512F__`.

---

## 2. Kernels

Todos en `src/image_core.hpp`, sin dependencias de Windows, con contrato de
bits verificado por `tests/image_core_test.exe`.

| Kernel | Función | Ruta SSE4.1/SSE4.2 | Ruta de reserva |
| --- | --- | --- | --- |
| Intercambio R↔B + detección de alfa | `SwapRbDetectAlpha` | `PSHUFB` (1 instr. por 4 px) + acumulador XOR + `PTEST` | máscaras/desplazamientos (SSE2) |
| Detección de transparencia | `DetectAnyNonOpaque` | acumulador XOR + `PTEST` | `PCMPEQB` + `PMOVMSKB` |
| Premultiplicado alfa | `PremultiplyBgra`, `CopyPremultipliedBgra` | `PSHUFB` para replicar el alfa + `PBLENDVB` | 4 operaciones de desplazamiento + máscaras |
| Horneado de efectos | `BakeBgra` | escalar escrito para auto-vectorización | — |
| Rotación / espejo | `RotateBgra`, `FlipBgra`, `TransformBgra` | `memcpy` por fila + recorrido por columnas | — |
| Baldosa de ajedrez | `FillCheckerTileBgra` | `memcpy` por fila con patrón precalculado | — |

### Cambios de este pase

- **Intercambio R↔B**: `_mm_shuffle_epi8` sustituye a 2 AND + 2 desplazamientos +
  2 OR. Las máscaras de bytes están escritas a mano y se **verifican por
  emulación** en `check_simd_policy.py` (comprueba que el `PSHUFB` produce
  exactamente `R,G,B,A` a partir de `B,G,R,A`).
- **Detección de opacidad**: un único `_mm_testz_si128` reemplaza
  `movemask` + comparación contra `0xFFFF`.
- **Premultiplicado**: `PSHUFB` coloca el alfa de cada píxel en sus carriles de
  16 bits en una instrucción (antes cuatro), y `PBLENDVB` restituye el alfa
  original (antes dos AND + OR). La identidad de redondeo
  `(c·a + 127) / 255 ≡ ((c·a + 127) · 0x8081) >> 23` se mantiene **exacta** y el
  verificador la comprueba exhaustivamente para los 65 536 pares `(c, a)`.
- **Cobertura de SIMD**: los tests anteriores usaban `std::vector`, cuyas
  direcciones solo están garantizadas a 16 B. Con `kPixelAlignment = 64 B` la
  comprobación `IsAligned(px, 64)` era falsa y **solo se ejecutaba la cola
  escalar**: los kernels vectorizados no estaban cubiertos. El nuevo test
  `TestSimdKernelsAligned` reserva con el asignador real (64 B), **verifica la
  alineación** y compara contra la referencia escalar byte a byte.

### Lo que NO se vectorizó, y por qué

`BakeBgra` con escala de grises mantiene el bucle escalar. La división entera por
`kLumaDivisor` (1000) la convierte el compilador en una multiplicación mágica
**exacta** para todo el rango de sumas (0..255000), y el bucle es trivialmente
auto-vectorizable por tener multiplicadores constantes y cero ramas. Escribir la
constante mágica a mano no aporta velocidad y sí riesgo de romper la
bit-exactitud que fija `LumaBt601`. Además, ese horneado ocurre **una vez por
cambio de efecto**, nunca por fotograma.

---

## 3. Cómo medir

### Banco de kernels

```cmd
:: MSVC, baseline SSE4.2 (la configuración de release)
cl /nologo /EHsc /std:c++20 /O2 /arch:SSE4.2 /DARTPICST_TARGET_SSE42=1 ^
   /utf-8 /W4 /permissive- /I. /Iinclude ^
   /Fe:"build\kernel_bench.exe" tests\kernel_bench.cpp /link kernel32.lib
build\kernel_bench.exe                 :: 2 097 152 px, 30 repeticiones
build\kernel_bench.exe 4194304 50      :: píxeles y repeticiones explícitos
```

O bien, con el pipeline del proyecto:

```cmd
make bench
powershell -ExecutionPolicy Bypass -File scripts\verify.ps1 -Bench
```

### Comparación SSE2 vs SSE4.2

El banco mide lo que se compila, así que la comparación es directa: compila el
mismo archivo dos veces, cambiando solo el baseline, y compara.

```cmd
:: 1) baseline SSE4.2 (actual)
cl /nologo /EHsc /std:c++20 /O2 /arch:SSE4.2 /DARTPICST_TARGET_SSE42=1 ^
   /utf-8 /W4 /permissive- /I. /Iinclude ^
   /Fe:"build\kernel_bench_sse42.exe" tests\kernel_bench.cpp /link kernel32.lib

:: 2) baseline SSE2 (referencia histórica: NO usar como release)
cl /nologo /EHsc /std:c++20 /O2 /utf-8 /W4 /permissive- /I. /Iinclude ^
   /Fe:"build\kernel_bench_sse2.exe" tests\kernel_bench.cpp /link kernel32.lib

build\kernel_bench_sse42.exe > bench_sse42.txt
build\kernel_bench_sse2.exe  > bench_sse2.txt
```

**Metodología.** El banco informa del **mejor** tiempo de N repeticiones (la
pasada menos contaminada por el planificador), no de la media: interesa la
capacidad del kernel, no el ruido del sistema. Los buffers se reservan con el
asignador real del proyecto (64 B) y el número de píxeles se redondea a múltiplo
de 8 para que **la cola escalar quede vacía** y la cifra describa solo la ruta
vectorizada. El ancho de banda se calcula sobre los bytes realmente tocados
(lectura + escritura en los kernels in situ).

**Salvedades.** El banco usa un restablecimiento con `memcpy` fuera del cronómetro
en los kernels in situ; ese `memcpy` no se cuenta. Los kernels sin ramas por píxel
no se ven sesgados por el patrón de datos, y el patrón es determinista.

---

## 4. Presupuesto de memoria y detalle de origen

`MaxDimensionLimit()` (`src/main.cpp`) sustituye al antiguo tope fijo de 3840 px,
que **estrangulaba el detalle de origen**: una foto de 6000×4000 se reducía durante
la carga, así que el zoom al 100 % ya no mostraba los píxeles reales y el análisis
a 512× se hacía sobre una imagen ya interpolada.

| Parámetro | Valor | Razón |
| --- | --- | --- |
| Presupuesto de decodificación | 100 MB | búfer BGRA = `ancho · alto · 4 B` |
| Lado máximo resultante | **5000 px** | `√(100 MB / 4)` = 5000 (95,4 MB) |
| Suelo | 3840 px | nunca menos que antes |
| Techo duro | 16384 px | 16384² · 4 B = 1 GiB |
| Ajuste del usuario | `ARTPICST_MAX_DIMENSION` (512..16384) | máquinas con mucha RAM |

El valor efectivo se registra en el banner de arranque del log, junto a la versión
y al baseline SIMD, para poder interpretar cualquier informe de fallo.

Otros límites que afectan al consumo:

| Constante | Valor | Efecto |
| --- | --- | --- |
| `MAX_CACHE_BYTES` | 64 MB | tope de la caché de imágenes vecinas |
| `CACHE_SIZE` | 4 | 2 previas + 2 siguientes |
| `MAX_FILE_BYTES` | 300 MB | rechaza archivos mayores antes de leerlos |
| `MAX_GIF_BYTES` | 64 MB | presupuesto del GIF animado |
| `MAX_GIF_FRAMES` | 120 | fotogramas decodificados como máximo |

---

## 5. Resultados (pendientes de medir)

Rellena esta tabla con la salida de `kernel_bench.exe`. `ns/px` = nanosegundos por
píxel; `GiB/s` = ancho de banda efectivo sobre los bytes tocados.

**Entorno:** _(CPU, RAM, versión de Windows, versión de MSVC, fecha)_

| Kernel | SSE2 ns/px | SSE4.2 ns/px | Mejora | SSE4.2 GiB/s |
| --- | --- | --- | --- | --- |
| `SwapRbDetectAlpha` (in situ) | — | — | — | — |
| `DetectAnyNonOpaque` (solo lectura) | — | — | — | — |
| `PremultiplyBgra` (in situ) | — | — | — | — |
| `CopyPremultipliedBgra` | — | — | — | — |
| `BakeBgra` (sin efecto = memcpy) | — | — | — | — |
| `BakeBgra` (escala de grises) | — | — | — | — |
| `BakeBgra` (negativo, LUT) | — | — | — | — |
| `BakeBgra` (ultra-claridad, LUT) | — | — | — | — |
| `RotateBgra` 90° (1024×1024) | — | — | — | — |
| `FillCheckerTileBgra` (64×64) | — | — | — | — |

### Qué esperar razonablemente

Sin cifras medidas no se afirma ninguna mejora. Lo que **sí** es verificable por
inspección es el número de instrucciones por iteración:

| Kernel | SSE2 | SSE4.2 | Cambio |
| --- | --- | --- | --- |
| Intercambio R↔B (por 4 px) | 5 instrucciones | **1** (`PSHUFB`) | −4 |
| Detección de opacidad (final) | `movemask` + comparación | **1** (`PTEST`) | −1 |
| Réplica de alfa (por 4 px) | 4 instrucciones | **1** (`PSHUFB`) | −3 |
| Restitución de alfa (por 4 px) | 3 instrucciones | **1** (`PBLENDVB`) | −2 |

El límite superior real lo pone el ancho de banda: en los kernels in situ (leer +
escribir 4 B/píxel) el techo práctico es la velocidad de la memoria, no la ALU.

---

## 6. Cómo evitar regresiones

1. `tests/check_simd_policy.py` — prohibición de AVX, presencia de los flags y
   oráculo de las identidades enteras y de las máscaras `PSHUFB`. Se ejecuta en
   `build.ps1`, `build.bat`, `build_mingw.bat`, `Makefile`, `verify.ps1` y los
   tres workflows de CI.
2. `tests/image_core_test.exe` — contrato de bits de cada kernel contra su
   referencia escalar, ahora **con buffers alineados a 64 B** para cubrir de
   verdad la ruta vectorizada.
3. `tests/kernel_bench.exe` — vigilancia de rendimiento. Guarda la salida en el
   repositorio (o en el artefacto de CI) para poder comparar entre versiones.
4. Fuzzing nocturno con AddressSanitizer (`.github/workflows/nightly-fuzz.yml`)
   — detecta lecturas fuera de límites y fugas en el asignador alineado.
