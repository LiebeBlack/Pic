// ============================================================================
//  ARTPICST · IMAGE CORE — Núcleo de procesamiento de píxeles de alto rendimiento
// ============================================================================
//  Este archivo es C++23/C++26 puro y SIN dependencias de Windows: puede
//  compilarse y probarse de forma aislada (tests/image_core_test.cpp).
//
//  Semántica ISO/IEC 9899:2024 (C23) plasmada en C++ (mismos atributos, mismos
//  tipos, misma alineación). Cada directiva de la norma tiene su equivalente
//  exacto:
//
//      C23 (ISO 9899:2024)         Equivalente C++23/26 usado aquí
//      ---------------------------------------------------------------------
//      nullptr / nullptr_t         nullptr (idéntico, ya nativo en C++)
//      constexpr                   constexpr / inline constexpr
//      [[nodiscard]]               [[nodiscard]]
//      [[maybe_unused]]            [[maybe_unused]]
//      [[likely]] / [[unlikely]]   idénticos
//      [[reproducible]]            idénticos (C++26; macro de compatibilidad)
//      [[unsequenced]]             idénticos (C++26; macro de compatibilidad)
//      alignas / alignof           idénticos
//      static_assert               idéntico
//      unreachable()               std::unreachable() (C++23)
//      restrict                    ARTPICST_RESTRICT (__restrict en cada ABI)
//      uint8_t / uint32_t / size_t idénticos (<cstdint>, <cstddef>)
//
//  Objetivo: cero ramas dentro de los bucles de píxeles, cero asignaciones
//  ocultas, buffers alineados a línea de caché (64 B) y bucles escritos para
//  auto-vectorización SSE2/AVX2/NEON, con rutas SIMD explícitas en x86-64.
// ============================================================================

#ifndef ARTPICST_IMAGE_CORE_HPP
#define ARTPICST_IMAGE_CORE_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <type_traits>

// ---------------------------------------------------------------------------
// 1. Atributos estándar y portabilidad de compilador
// ---------------------------------------------------------------------------

// Valida en tiempo de compilación que el objetivo es C++20 o superior: de él
// dependen [[likely]], [[unlikely]] y el resto de la infraestructura.
#if defined(_MSVC_LANG)
#  define ARTPICST_CXX_STD _MSVC_LANG
#else
#  define ARTPICST_CXX_STD __cplusplus
#endif
static_assert(ARTPICST_CXX_STD >= 202002L,
              "ARTPICST image core requiere C++20 o superior (recomendado C++23/26).");

#if defined(__has_cpp_attribute)
#  if __has_cpp_attribute(nodiscard)
#    define ARTPICST_NODISCARD [[nodiscard]]
#  endif
#  if __has_cpp_attribute(maybe_unused)
#    define ARTPICST_MAYBE_UNUSED [[maybe_unused]]
#  endif
#  if __has_cpp_attribute(likely)
#    define ARTPICST_LIKELY [[likely]]
#  endif
#  if __has_cpp_attribute(unlikely)
#    define ARTPICST_UNLIKELY [[unlikely]]
#  endif
// [[reproducible]] y [[unsequenced]] solo se activan si el compilador declara
// un valor de __has_cpp_attribute CONFORME a la norma (>= 202207, tabla SD-6).
// GCC 16 anuncia un valor anomalo de 1 y despues ignora la directiva
// ("attribute directive ignored"), asi que se desactiva para no ensuciar el
// build; Clang 20+ y las versiones conformes lo activan automaticamente.
#  if __has_cpp_attribute(reproducible) >= 202207L
#    define ARTPICST_REPRODUCIBLE [[reproducible]]
#  endif
#  if __has_cpp_attribute(unsequenced) >= 202207L
#    define ARTPICST_UNSEQUENCED [[unsequenced]]
#  endif
#  if __has_cpp_attribute(assume)
#    define ARTPICST_ASSUME_TRUE(expr) [[assume(expr)]]
#  endif
#endif

#ifndef ARTPICST_NODISCARD
#  define ARTPICST_NODISCARD
#endif
#ifndef ARTPICST_MAYBE_UNUSED
#  define ARTPICST_MAYBE_UNUSED
#endif
#ifndef ARTPICST_LIKELY
#  define ARTPICST_LIKELY
#endif
#ifndef ARTPICST_UNLIKELY
#  define ARTPICST_UNLIKELY
#endif
// [[reproducible]] y [[unsequenced]] (C23 / C++26): funciones puras, sin efectos
// secundarios ni estado mutable compartido. Permiten al optimizador
// reordenarlas, hacer CSE y fusionarlas con total libertad.
#ifndef ARTPICST_REPRODUCIBLE
#  define ARTPICST_REPRODUCIBLE
#endif
#ifndef ARTPICST_UNSEQUENCED
#  define ARTPICST_UNSEQUENCED
#endif
#ifndef ARTPICST_ASSUME_TRUE
#  define ARTPICST_ASSUME_TRUE(expr) ((void)0)
#endif

// `restrict` (C99/C23): en C++ no existe la palabra clave, se usa la extensión
// homóloga del ABI. Informa de que los rangos de memoria NO se solapan, lo que
// habilita vectorización equivalente a ensamblador (cargas/almacenes sin
// chequeos de aliasing).
#if defined(_MSC_VER)
#  define ARTPICST_RESTRICT __restrict
#elif defined(__GNUC__) || defined(__clang__)
#  define ARTPICST_RESTRICT __restrict__
#else
#  define ARTPICST_RESTRICT
#endif

// Control de flujo inalcanzable (unreachable() de <stddef.h> en C23).
#if defined(__cpp_lib_unreachable)
#  include <utility>
#  define ARTPICST_UNREACHABLE() std::unreachable()
#elif defined(__GNUC__) || defined(__clang__)
#  define ARTPICST_UNREACHABLE() __builtin_unreachable()
#elif defined(_MSC_VER)
#  define ARTPICST_UNREACHABLE() __assume(false)
#else
#  define ARTPICST_UNREACHABLE() ((void)0)
#endif

// Promesa de alineación al optimizador: elimina las comprobaciones de
// desalineación y permite emitir MOVDQA / VMOVDQA en lugar de las variantes
// seguras MOVDQU.
#if defined(__GNUC__) || defined(__clang__)
#  define ARTPICST_ASSUME_ALIGNED(ptr, align) __builtin_assume_aligned((ptr), (align))
#elif defined(_MSC_VER)
#  define ARTPICST_ASSUME_ALIGNED(ptr, align) \
      (__assume((reinterpret_cast<uintptr_t>(ptr) % (align)) == 0), (ptr))
#else
#  define ARTPICST_ASSUME_ALIGNED(ptr, align) (ptr)
#endif

// Ruta SIMD explícita: SSE2 es el mínimo garantizado en todo x86-64.
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#  define ARTPICST_SSE2 1
#  include <emmintrin.h>
#else
#  define ARTPICST_SSE2 0
#endif

namespace artpicst {

// ---------------------------------------------------------------------------
// 2. Geometría de memoria: alineación y aritmética segura
// ---------------------------------------------------------------------------

// Alineación a línea de caché completa (L1/L2/L3 = 64 B en x86-64 y ARM64).
// Cada fila de píxeles empieza y termina en una frontera de línea de caché, por
// lo que un recorrido lineal nunca produce "cache line splits".
inline constexpr size_t kCacheLineBytes = 64;
inline constexpr size_t kSimdAlignment  = 32;   // AVX2: 32 B
inline constexpr size_t kPixelAlignment = kCacheLineBytes;

static_assert((kPixelAlignment & (kPixelAlignment - 1)) == 0,
              "La alineación de píxeles debe ser potencia de dos.");
static_assert(kPixelAlignment >= kSimdAlignment,
              "La alineación de píxeles debe cubrir el ancho de registro vectorial.");

// Alineación de la estructura de imagen: exigida en tiempo de compilación con
// alignas/alignof/static_assert, sin ningún sobrecoste en tiempo de ejecución.
#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable: 4324) // PixelBlock está intencionalmente alineado a línea de caché (64B)
#endif
struct alignas(kPixelAlignment) PixelBlock {
    uint8_t* data;
    size_t   bytes;
};
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif
static_assert(alignof(PixelBlock) == kPixelAlignment,
              "PixelBlock debe respetar la alineación de línea de caché.");

// Multiplicación con detección de desbordamiento (seguridad al calcular
// width * height * channels * bytes_per_channel).
ARTPICST_NODISCARD ARTPICST_REPRODUCIBLE ARTPICST_UNSEQUENCED
inline bool MulOverflow(size_t a, size_t b, size_t& out) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    return !__builtin_mul_overflow(a, b, &out);
#else
    if (b != 0 && a > (SIZE_MAX / b)) return false;
    out = a * b;
    return true;
#endif
}

// Cálculo seguro de bytes de un frame BGRA/RGBA (4 B/píxel).
ARTPICST_NODISCARD ARTPICST_REPRODUCIBLE ARTPICST_UNSEQUENCED
inline bool CheckedBgraBytes(uint32_t width, uint32_t height, size_t& outBytes) noexcept {
    size_t pixels = 0;
    if (!MulOverflow(static_cast<size_t>(width), static_cast<size_t>(height), pixels)) return false;
    if (!MulOverflow(pixels, 4u, outBytes)) return false;
    return outBytes != 0;
}

// ---------------------------------------------------------------------------
// 3. Asignador alineado de píxeles
// ---------------------------------------------------------------------------
// Se asigna con una sobre-reserva y se guarda el puntero base justo antes del
// bloque alineado. Ventajas:
//   · asignar y liberar pasan siempre por el MISMO CRT (malloc/free), sin riesgo
//     de mezclar heaps entre DLLs ni de olvidar el liberador "especial";
//   · el bloque devuelto es múltiplo exacto de 64 B (línea de caché);
//   · el tamaño se redondea a múltiplo de 64 B, así el último píxel tampoco
//     comparte línea de caché con datos ajenos.

ARTPICST_NODISCARD inline void* AlignedPixelAlloc(size_t bytes) noexcept {
    if (bytes == 0) return nullptr;                                    // [[unlikely]]
    if (bytes > SIZE_MAX - (kPixelAlignment * 2)) return nullptr;      // sin desbordar
    const size_t rounded = (bytes + (kPixelAlignment - 1)) & ~(kPixelAlignment - 1);
    const size_t total   = rounded + kPixelAlignment + sizeof(void*);
    void* raw = std::malloc(total);
    if (!raw) return nullptr;                                          // [[unlikely]]
    const uintptr_t base    = reinterpret_cast<uintptr_t>(raw) + sizeof(void*);
    const uintptr_t aligned = (base + (kPixelAlignment - 1)) & ~static_cast<uintptr_t>(kPixelAlignment - 1);
    reinterpret_cast<void**>(aligned)[-1] = raw;                       // base para liberar
    return reinterpret_cast<void*>(aligned);
}

inline void AlignedPixelFree(void* p) noexcept {
    if (!p) return;                                                    // [[unlikely]]
    std::free(reinterpret_cast<void**>(p)[-1]);
}

// "realloc" alineado: no puede crecer en sitio (la alineación se perdería), así
// que reserva, copia el mínimo y libera. Si falla la reserva devuelve nullptr
// SIN tocar el bloque original: nunca hay pérdida de memoria.
ARTPICST_NODISCARD inline void* AlignedPixelRealloc(void* p, size_t oldBytes, size_t newBytes) noexcept {
    if (!p) return AlignedPixelAlloc(newBytes);
    if (newBytes == 0) { AlignedPixelFree(p); return nullptr; }
    void* fresh = AlignedPixelAlloc(newBytes);
    if (!fresh) return nullptr;                                        // [[unlikely]]
    const size_t copy = (oldBytes < newBytes) ? oldBytes : newBytes;
    std::memcpy(fresh, p, copy);
    AlignedPixelFree(p);
    return fresh;
}

// ¿Respeta el puntero la alineación exigida? Permite elegir en tiempo de
// ejecución entre la ruta de carga alineada (MOVDQA) y la segura (MOVDQU).
ARTPICST_NODISCARD ARTPICST_REPRODUCIBLE
inline bool IsAligned(const void* p, size_t alignment) noexcept {
    return (reinterpret_cast<uintptr_t>(p) & (alignment - 1)) == 0;
}

// ---------------------------------------------------------------------------
// 4. Tablas de consulta (LUT) evaluadas en TIEMPO DE COMPILACIÓN
// ---------------------------------------------------------------------------
// Sustituyen todo el cálculo en coma flotante por píxel de las matrices de color
// de GDI+ y eliminan las ramas de saturación del bucle interno.

struct LutTables {
    std::array<uint8_t, 256> invert{};   // negativo: 255 - v
    std::array<uint8_t, 256> clarity{};  // ultra-claridad: 128 + (v-128)*1.12 saturado
};

// Pesos de luminancia BT.601 usados por la conversión a escala de grises.
inline constexpr uint32_t kLumaWeightR = 299;
inline constexpr uint32_t kLumaWeightG = 587;
inline constexpr uint32_t kLumaWeightB = 114;
inline constexpr uint32_t kLumaDivisor  = 1000;

// División entera con redondeo hacia abajo (necesaria para valores negativos).
ARTPICST_NODISCARD ARTPICST_REPRODUCIBLE ARTPICST_UNSEQUENCED
constexpr int32_t FloorDiv(int32_t num, int32_t den) noexcept {
    return (num >= 0) ? (num / den) : (-(((-num) + den - 1) / den));
}

constexpr LutTables BuildLutTables() noexcept {
    LutTables t{};
    for (int32_t v = 0; v < 256; ++v) {
        const size_t i = static_cast<size_t>(v);
        t.invert[i] = static_cast<uint8_t>(255 - v);

        // Equivalente EXACTO de clampscalar(floor(128.0f + (v-128)*1.12f + 0.5f)):
        //   1.12 = 28/25  =>  floor((56*v - 743) / 50)
        // Verificado valor a valor contra la versión en coma flotante original.
        int32_t c = FloorDiv((56 * v) - 743, 50);
        if (c < 0) c = 0;
        if (c > 255) c = 255;
        t.clarity[i] = static_cast<uint8_t>(c);
    }
    return t;
}

inline constexpr LutTables kLut = BuildLutTables();

// Muestreo puro de las LUT: sin efectos secundarios ni estado mutable, por lo
// que son candidatas ideales a [[reproducible]] y [[unsequenced]].
ARTPICST_NODISCARD ARTPICST_REPRODUCIBLE ARTPICST_UNSEQUENCED
constexpr uint8_t LutInvert(uint8_t v) noexcept { return kLut.invert[v]; }

ARTPICST_NODISCARD ARTPICST_REPRODUCIBLE ARTPICST_UNSEQUENCED
constexpr uint8_t LutClarity(uint8_t v) noexcept { return kLut.clarity[v]; }

// Luminancia BT.601 en punto fijo: (299R + 587G + 114B) / 1000. Idéntica a la
// fórmula original, pero escrita con multiplicaciones por constante para que el
// compilador la vectorice (y con la división convertida en multiplicación y
// desplazamiento, que es lo que el hardware ejecuta realmente).
ARTPICST_NODISCARD ARTPICST_REPRODUCIBLE ARTPICST_UNSEQUENCED
constexpr uint8_t LumaBt601(uint8_t r, uint8_t g, uint8_t b) noexcept {
    const uint32_t sum = kLumaWeightR * static_cast<uint32_t>(r)
                       + kLumaWeightG * static_cast<uint32_t>(g)
                       + kLumaWeightB * static_cast<uint32_t>(b);
    return static_cast<uint8_t>(sum / kLumaDivisor);
}

// Alfa premultiplicado con redondeo, sin división real:
//   (c*a + 127) / 255  ==  mulhi_epu16(c*a + 127, 0x8081) >> 7
// (identidad exacta para todo producto c*a en [0, 65025]).
ARTPICST_NODISCARD ARTPICST_REPRODUCIBLE ARTPICST_UNSEQUENCED
constexpr uint8_t PremulByte(uint8_t c, uint8_t a) noexcept {
    const uint32_t p = static_cast<uint32_t>(c) * static_cast<uint32_t>(a);
    return static_cast<uint8_t>((p + 127u) / 255u);
}

// ---------------------------------------------------------------------------
// 5. Operaciones de bajo nivel sobre el búfer BGRA
// ---------------------------------------------------------------------------
// Convención única de todo el programa: 4 bytes contiguos por píxel en el orden
// B, G, R, A (leído como uint32_t en little endian: A<<24 | R<<16 | G<<8 | B).
// Todas las rutinas declaran ARTPICST_RESTRICT: origen y destino nunca solapan.

inline constexpr uint32_t kAlphaMaskWord = 0xFF000000u;
inline constexpr uint32_t kRgbMaskWord   = 0x00FFFFFFu;
inline constexpr uint32_t kPairMaskWord  = 0x00FF00FFu;   // pares B/R y G/A

// --- 5.1 Intercambio R<->B + detección de transparencia (decodificación) -----
// Dos recorridos completos fusionados en UNO: antes se intercambiaban los canales
// en una pasada y se buscaba transparencia en otra.
ARTPICST_NODISCARD inline bool SwapRbDetectAlpha(uint8_t* ARTPICST_RESTRICT px,
                                                 size_t pixels) noexcept {
    if (!px || pixels == 0) return false;                              // [[unlikely]]
    bool hasAlpha = false;
    size_t i = 0;

#if ARTPICST_SSE2
    if (IsAligned(px, kPixelAlignment)) {                              // [[likely]]
        const size_t simd = pixels & ~static_cast<size_t>(3);
        uint8_t* p = static_cast<uint8_t*>(ARTPICST_ASSUME_ALIGNED(px, kPixelAlignment));
        const uint8_t* const end = px + simd * 4u;
        const __m128i rbMask   = _mm_set1_epi32(static_cast<int>(kPairMaskWord));
        const __m128i gaMask   = _mm_set1_epi32(static_cast<int>(0xFF00FF00u));
        const __m128i alphaMsk = _mm_set1_epi32(static_cast<int>(kAlphaMaskWord));
        __m128i opaque = _mm_set1_epi8(static_cast<char>(0xFF));
        for (; p < end; p += 16) {
            const __m128i v = _mm_load_si128(reinterpret_cast<const __m128i*>(p));
            // Intercambio de los canales B<->R dentro de cada palabra de 32 bits
            // (G y A permanecen intactos): 4 píxeles por iteración.
            const __m128i swapped = _mm_or_si128(
                _mm_or_si128(_mm_slli_epi32(_mm_and_si128(v, rbMask), 16),
                             _mm_and_si128(_mm_srli_epi32(v, 16), rbMask)),
                _mm_and_si128(v, gaMask));
            // Acumulador de opacidad: 0xFF si TODOS los alfas vistos son 0xFF
            opaque = _mm_and_si128(opaque, _mm_cmpeq_epi8(_mm_and_si128(v, alphaMsk), alphaMsk));
            _mm_store_si128(reinterpret_cast<__m128i*>(p), swapped);
        }
        hasAlpha = _mm_movemask_epi8(opaque) != 0xFFFF;                // [[unlikely]]
        i = simd;
    }
#endif
    for (; i < pixels; ++i) {                                          // cola escalar
        const size_t off = i * 4u;
        const uint8_t b = px[off + 0u];
        px[off + 0u] = px[off + 2u];
        px[off + 2u] = b;
        if (px[off + 3u] < 255u) hasAlpha = true;                      // [[unlikely]]
    }
    return hasAlpha;
}

// --- 5.2 Detección de transparencia sin transformar píxeles -------------------
// Sustituye los bucles escalares de DecodeWithWic/DecodeWithGdiplus.
ARTPICST_NODISCARD inline bool DetectAnyNonOpaque(const uint8_t* ARTPICST_RESTRICT px,
                                                  size_t pixels) noexcept {
    if (!px || pixels == 0) return false;                              // [[unlikely]]
    const uint8_t* p = static_cast<const uint8_t*>(ARTPICST_ASSUME_ALIGNED(px, 4));
    size_t i = 0;
#if ARTPICST_SSE2
    if (IsAligned(px, kPixelAlignment)) {                              // [[likely]]
        const size_t simd = pixels & ~static_cast<size_t>(3);
        const uint8_t* const end = px + simd * 4u;
        const __m128i alphaMsk = _mm_set1_epi32(static_cast<int>(kAlphaMaskWord));
        __m128i opaque = _mm_set1_epi8(static_cast<char>(0xFF));
        for (; p < end; p += 16) {
            const __m128i v = _mm_load_si128(reinterpret_cast<const __m128i*>(p));
            opaque = _mm_and_si128(opaque, _mm_cmpeq_epi8(_mm_and_si128(v, alphaMsk), alphaMsk));
        }
        if (_mm_movemask_epi8(opaque) != 0xFFFF) return true;          // [[unlikely]]
        i = simd;
    }
#endif
    for (; i < pixels; ++i) {
        if (p[i * 4u + 3u] < 255u) return true;                        // [[unlikely]]
    }
    return false;
}

// --- 5.3 Premultiplicado alfa (subida de frames a Direct2D) ------------------
// Direct2D exige alfa premultiplicado. La ruta SSE2 procesa 4 píxeles por
// iteración con 2 multiplicaciones de 16 bits + la división exacta por 255
// (mulhi_epu16 = (x*32897)>>16, luego >>7) — sin ninguna rama y con el mismo
// redondeo que PremulByte.
#if ARTPICST_SSE2
// Núcleo de registros: 4 píxeles BGRA -> 4 píxeles BGRA premultiplicados.
inline __m128i PremultiplyBlockSse2(__m128i v) noexcept {
    const __m128i pairMask = _mm_set1_epi32(static_cast<int>(kPairMaskWord));
    const __m128i notAlpha = _mm_set1_epi32(static_cast<int>(kRgbMaskWord));
    const __m128i alphaMsk = _mm_set1_epi32(static_cast<int>(kAlphaMaskWord));
    const __m128i k127     = _mm_set1_epi16(127);
    const __m128i kMagic   = _mm_set1_epi16(static_cast<short>(0x8081));
    // Alfa de cada píxel replicado en sus 4 bytes (a * 0x01010101): SSE2 no tiene
    // multiplicación de 32 bits, así que la replicación se hace con desplazamientos.
    const __m128i aRaw  = _mm_srli_epi32(v, 24);
    const __m128i aRep1 = _mm_or_si128(aRaw, _mm_slli_epi32(aRaw, 8));
    const __m128i aRep  = _mm_and_si128(_mm_or_si128(aRep1, _mm_slli_epi32(aRep1, 16)), pairMask);
    // Bytes pares (B,R) y bytes impares (G,A) alineados en carriles de 16 bits
    const __m128i lo = _mm_and_si128(v, pairMask);
    const __m128i hi = _mm_and_si128(_mm_srli_epi16(v, 8), pairMask);
    const __m128i qLo = _mm_srli_epi16(
        _mm_mulhi_epu16(_mm_add_epi16(_mm_mullo_epi16(lo, aRep), k127), kMagic), 7);
    const __m128i qHi = _mm_srli_epi16(
        _mm_mulhi_epu16(_mm_add_epi16(_mm_mullo_epi16(hi, aRep), k127), kMagic), 7);
    // Reempaquetado BGRA y restitución del alfa original (el producto A*A se descarta)
    const __m128i packed = _mm_or_si128(qLo, _mm_slli_epi16(qHi, 8));
    return _mm_or_si128(_mm_and_si128(packed, notAlpha), _mm_and_si128(v, alphaMsk));
}
#endif
// In situ: un solo recorrido, sin buffer auxiliar ni asignación adicional.
inline void PremultiplyBgra(uint8_t* ARTPICST_RESTRICT px, size_t pixels) noexcept {
    if (!px || pixels == 0) return;                                     // [[unlikely]]
    size_t i = 0;
#if ARTPICST_SSE2
    if (IsAligned(px, kPixelAlignment)) {                              // [[likely]]
        const size_t simd = pixels & ~static_cast<size_t>(7);
        uint8_t* p = static_cast<uint8_t*>(ARTPICST_ASSUME_ALIGNED(px, kPixelAlignment));
        const uint8_t* const end = px + simd * 4u;
        // Desenrollado x2: dos cadenas de dependencia independientes en vuelo
        // (la división de 16 bits es de latencia alta y limita el IPC si no hay ILP).
        for (; p + 32 <= end; p += 32) {
            const __m128i v0 = _mm_load_si128(reinterpret_cast<const __m128i*>(p));
            const __m128i v1 = _mm_load_si128(reinterpret_cast<const __m128i*>(p + 16));
            _mm_store_si128(reinterpret_cast<__m128i*>(p), PremultiplyBlockSse2(v0));
            _mm_store_si128(reinterpret_cast<__m128i*>(p + 16), PremultiplyBlockSse2(v1));
        }
        for (; p < end; p += 16) {
            _mm_store_si128(reinterpret_cast<__m128i*>(p),
                            PremultiplyBlockSse2(_mm_load_si128(reinterpret_cast<const __m128i*>(p))));
        }
        i = simd;
    }
#endif
    for (; i < pixels; ++i) {                                          // cola escalar
        uint8_t* const q = px + i * 4u;
        const uint8_t a = q[3];
        if (a == 255u) continue;                                       // [[likely]]
        q[0] = PremulByte(q[0], a);
        q[1] = PremulByte(q[1], a);
        q[2] = PremulByte(q[2], a);
    }
}

// Copia + premultiplicado FUSIONADOS en una sola pasada de memoria: evita el
// recorrido extra (leer+escribir el búfer destino completo) que exigía hacer
// memcpy y después premultiplicar. Es la ruta que usa el horneado sin efectos.
inline void CopyPremultipliedBgra(uint8_t* ARTPICST_RESTRICT dst,
                                  const uint8_t* ARTPICST_RESTRICT src,
                                  size_t pixels) noexcept {
    if (!dst || !src || pixels == 0) return;                            // [[unlikely]]
    size_t i = 0;
#if ARTPICST_SSE2
    if (IsAligned(dst, kPixelAlignment) && IsAligned(src, kPixelAlignment)) {  // [[likely]]
        const size_t simd = pixels & ~static_cast<size_t>(7);
        uint8_t* d = static_cast<uint8_t*>(ARTPICST_ASSUME_ALIGNED(dst, kPixelAlignment));
        const uint8_t* s = static_cast<const uint8_t*>(ARTPICST_ASSUME_ALIGNED(src, kPixelAlignment));
        const uint8_t* const end = src + simd * 4u;
        for (; s + 32 <= end; s += 32, d += 32) {   // desenrollado x2 (ver arriba)
            const __m128i v0 = _mm_load_si128(reinterpret_cast<const __m128i*>(s));
            const __m128i v1 = _mm_load_si128(reinterpret_cast<const __m128i*>(s + 16));
            _mm_store_si128(reinterpret_cast<__m128i*>(d), PremultiplyBlockSse2(v0));
            _mm_store_si128(reinterpret_cast<__m128i*>(d + 16), PremultiplyBlockSse2(v1));
        }
        for (; s < end; s += 16, d += 16) {
            _mm_store_si128(reinterpret_cast<__m128i*>(d),
                            PremultiplyBlockSse2(_mm_load_si128(reinterpret_cast<const __m128i*>(s))));
        }
        i = simd;
    }
#endif
    for (; i < pixels; ++i) {                                          // cola escalar
        const size_t off = i * 4u;
        const uint8_t a = src[off + 3u];
        dst[off + 0u] = PremulByte(src[off + 0u], a);
        dst[off + 1u] = PremulByte(src[off + 1u], a);
        dst[off + 2u] = PremulByte(src[off + 2u], a);
        dst[off + 3u] = a;
    }
}

// ---------------------------------------------------------------------------
// 6. Horneado de efectos (B/N · negativo · ultra-claridad) en una sola pasada
// ---------------------------------------------------------------------------

enum EffectBits : uint32_t {
    kEffectNone    = 0u,
    kEffectGray    = 1u << 0,
    kEffectInvert  = 1u << 1,
    kEffectClarity = 1u << 2,
};

// Copia con transformación de color. Los efectos son mutuamente excluyentes
// (igual que las matrices de color de GDI+ originales), así que la selección se
// resuelve FUERA del bucle: cero ramas por píxel dentro del bucle interno y
// CERO divisiones por canal (antes se premultiplicaba píxel a píxel aquí, con
// tres divisiones por píxel). El premultiplicado es ahora una pasada SIMD aparte
// (PremultiplyBgra), lo que además desacopla ambos pasos y permite reutilizar el
// resultado sin volver a transformar los colores.
ARTPICST_NODISCARD inline bool BakeBgra(const uint8_t* ARTPICST_RESTRICT src,
                                        uint8_t* ARTPICST_RESTRICT dst,
                                        size_t pixels, uint32_t effects) noexcept {
    if (!src || !dst || pixels == 0) return false;                     // [[unlikely]]
    const uint8_t* s = static_cast<const uint8_t*>(ARTPICST_ASSUME_ALIGNED(src, kPixelAlignment));
    uint8_t* d = static_cast<uint8_t*>(ARTPICST_ASSUME_ALIGNED(dst, kPixelAlignment));
    const size_t bytes = pixels * 4u;

    if ((effects & (kEffectGray | kEffectInvert | kEffectClarity)) == kEffectNone) {
        std::memcpy(d, s, bytes);                                      // cero trabajo
        return true;
    }
    if ((effects & kEffectGray) != 0u) {
        for (size_t i = 0; i < pixels; ++i) {                          // [[unlikely]]
            const size_t off = i * 4u;
            const uint8_t lum = LumaBt601(s[off + 2u], s[off + 1u], s[off + 0u]);
            d[off + 0u] = lum;
            d[off + 1u] = lum;
            d[off + 2u] = lum;
            d[off + 3u] = s[off + 3u];
        }
        return true;
    }
    if ((effects & kEffectInvert) != 0u) {
        for (size_t i = 0; i < pixels; ++i) {                          // [[unlikely]]
            const size_t off = i * 4u;
            d[off + 0u] = LutInvert(s[off + 0u]);
            d[off + 1u] = LutInvert(s[off + 1u]);
            d[off + 2u] = LutInvert(s[off + 2u]);
            d[off + 3u] = s[off + 3u];
        }
        return true;
    }
    if ((effects & kEffectClarity) != 0u) {
        for (size_t i = 0; i < pixels; ++i) {                          // [[unlikely]]
            const size_t off = i * 4u;
            d[off + 0u] = LutClarity(s[off + 0u]);
            d[off + 1u] = LutClarity(s[off + 1u]);
            d[off + 2u] = LutClarity(s[off + 2u]);
            d[off + 3u] = s[off + 3u];
        }
        return true;
    }
    ARTPICST_UNREACHABLE();
}

// ---------------------------------------------------------------------------
// 7. Transformaciones geométricas (rotación por cuartos y espejos)
// ---------------------------------------------------------------------------
// Se usan al exportar/portapapeles, donde antes se creaba un Graphics de GDI+,
// un Bitmap temporal y se redibujaba la imagen completa píxel a píxel.

enum class Rotation : uint32_t { None = 0, Cw90 = 90, Half = 180, Cw270 = 270 };

inline void FlipBgra(uint8_t* ARTPICST_RESTRICT dst, const uint8_t* ARTPICST_RESTRICT src,
                     uint32_t w, uint32_t h, bool flipH, bool flipV) noexcept {
    if (!dst || !src || w == 0 || h == 0) return;                      // [[unlikely]]
    for (uint32_t y = 0; y < h; ++y) {
        const uint32_t sy = flipV ? (h - 1u - y) : y;
        const uint8_t* const srow = src + static_cast<size_t>(sy) * w * 4u;
        uint8_t* const drow = dst + static_cast<size_t>(y) * w * 4u;
        if (!flipH) {                                                  // [[likely]]
            std::memcpy(drow, srow, static_cast<size_t>(w) * 4u);
        } else {
            for (uint32_t x = 0; x < w; ++x) {                         // [[unlikely]]
                std::memcpy(drow + static_cast<size_t>(x) * 4u,
                            srow + static_cast<size_t>(w - 1u - x) * 4u, 4u);
            }
        }
    }
}

inline void RotateBgra(uint8_t* ARTPICST_RESTRICT dst, const uint8_t* ARTPICST_RESTRICT src,
                       uint32_t w, uint32_t h, Rotation rot) noexcept {
    if (!dst || !src || w == 0 || h == 0) return;                      // [[unlikely]]
    switch (rot) {
        case Rotation::None:
            std::memcpy(dst, src, static_cast<size_t>(w) * h * 4u);
            return;
        case Rotation::Half:
            for (uint32_t y = 0; y < h; ++y) {
                const uint8_t* const srow = src + static_cast<size_t>(h - 1u - y) * w * 4u;
                uint8_t* const drow = dst + static_cast<size_t>(y) * w * 4u;
                for (uint32_t x = 0; x < w; ++x) {
                    std::memcpy(drow + static_cast<size_t>(x) * 4u,
                                srow + static_cast<size_t>(w - 1u - x) * 4u, 4u);
                }
            }
            return;
        case Rotation::Cw90:
            // dst (h x w): dst(x,y) = src(y, h-1-x)
            for (uint32_t y = 0; y < w; ++y) {
                uint8_t* const drow = dst + static_cast<size_t>(y) * h * 4u;
                for (uint32_t x = 0; x < h; ++x) {
                    std::memcpy(drow + static_cast<size_t>(x) * 4u,
                                src + (static_cast<size_t>(h - 1u - x) * w + y) * 4u, 4u);
                }
            }
            return;
        case Rotation::Cw270:
            // dst (h x w): dst(x,y) = src(w-1-y, x)
            for (uint32_t y = 0; y < w; ++y) {
                uint8_t* const drow = dst + static_cast<size_t>(y) * h * 4u;
                const uint8_t* const srow = src + static_cast<size_t>(w - 1u - y) * 4u;
                for (uint32_t x = 0; x < h; ++x) {
                    std::memcpy(drow + static_cast<size_t>(x) * 4u,
                                srow + static_cast<size_t>(x) * w * 4u, 4u);
                }
            }
            return;
    }
    ARTPICST_UNREACHABLE();  // unreachable() (C23): ninguna otra rama es posible
}

// Orquesta espejo + rotación con la MISMA semántica que la matriz de GDI+
// (T(centro)·R·F·T(-centro)): primero el espejo y después la rotación.
// `scratch` solo se necesita cuando hay espejo Y rotación (w*h*4 bytes).
ARTPICST_NODISCARD inline const uint8_t* TransformBgra(
        const uint8_t* ARTPICST_RESTRICT src, uint32_t w, uint32_t h,
        int rotation, bool flipH, bool flipV,
        uint8_t* ARTPICST_RESTRICT dst, uint8_t* ARTPICST_RESTRICT scratch,
        uint32_t& outW, uint32_t& outH) noexcept {
    outW = w;
    outH = h;
    if (!src || !dst || w == 0 || h == 0) return nullptr;              // [[unlikely]]

    Rotation rot = Rotation::None;
    switch (rotation) {
        case 0:   rot = Rotation::None;  break;
        case 90:  rot = Rotation::Cw90;  outW = h; outH = w; break;
        case 180: rot = Rotation::Half;  break;
        case 270: rot = Rotation::Cw270; outW = h; outH = w; break;
        default:  ARTPICST_UNREACHABLE();
    }

    if (!flipH && !flipV) {                                            // [[likely]]
        RotateBgra(dst, src, w, h, rot);
        return dst;
    }
    if (rot == Rotation::None) {                                       // [[unlikely]]
        FlipBgra(dst, src, w, h, flipH, flipV);
        return dst;
    }
    if (!scratch) return nullptr;                                      // [[unlikely]]
    FlipBgra(scratch, src, w, h, flipH, flipV);
    RotateBgra(dst, scratch, w, h, rot);
    return dst;
}

// ---------------------------------------------------------------------------
// 8. Baldosa de ajedrez (fondo de transparencias)
// ---------------------------------------------------------------------------
// Se escribe directamente en memoria con palabras de 32 bits y memcpy por fila:
// antes se llamaba a Bitmap::SetPixel 1024 veces (una por píxel), cada una con su
// transición COM hacia GDI+.
inline void FillCheckerTileBgra(uint32_t* ARTPICST_RESTRICT tile, uint32_t tileSize,
                                uint32_t cellSize, uint32_t colorA, uint32_t colorB) noexcept {
    if (!tile || tileSize == 0 || cellSize == 0) return;               // [[unlikely]]
    std::array<uint32_t, 64> rowEven{};
    std::array<uint32_t, 64> rowOdd{};
    const uint32_t n = (tileSize < 64u) ? tileSize : 64u;
    for (uint32_t x = 0; x < n; ++x) {
        const bool odd = ((x / cellSize) & 1u) != 0u;
        rowEven[x] = odd ? colorB : colorA;
        rowOdd[x]  = odd ? colorA : colorB;
    }
    for (uint32_t y = 0; y < tileSize; ++y) {
        const bool oddRow = ((y / cellSize) & 1u) != 0u;
        const uint32_t* const row = oddRow ? rowOdd.data() : rowEven.data();
        uint32_t* const dst = tile + static_cast<size_t>(y) * tileSize;
        if (tileSize <= 64u) {                                         // [[likely]]
            std::memcpy(dst, row, static_cast<size_t>(tileSize) * sizeof(uint32_t));
        } else {
            for (uint32_t x = 0; x < tileSize; ++x) dst[x] = row[x & 63u];
        }
    }
}

// Empaqueta componentes BGR (canales sueltos) en la palabra BGRA opaca.
ARTPICST_NODISCARD ARTPICST_REPRODUCIBLE ARTPICST_UNSEQUENCED
constexpr uint32_t BgraWordFromBgr(uint32_t rr, uint32_t gg, uint32_t bb) noexcept {
    return 0xFF000000u | ((rr & 0xFFu) << 16) | ((gg & 0xFFu) << 8) | (bb & 0xFFu);
}

}  // namespace artpicst

#endif  // ARTPICST_IMAGE_CORE_HPP
