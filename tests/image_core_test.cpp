// ============================================================================
// image_core_test.cpp — Tests del núcleo de imagen (artpicst::image_core.hpp)
// ============================================================================
// Ejecutable de consola autónomo: compila la cabecera TAL CUAL se usa en el
// visor (sin dependencias de Windows-UI) y valida LUT constexpr, allocator
// alineado, premultiplicado alfa, horneado de efectos, transformaciones
// geométricas y baldosa de ajedrez.
//
// Compilación (MSVC, desde la raíz del repo):
//   cl /nologo /EHsc /std:c++20 /O2 /utf-8 /W4 /I. /Iinclude ^
//      /Fe:build\image_core_test.exe tests\image_core_test.cpp
// Compilación (MinGW-w64):
//   g++ -std=c++20 -O2 -Wall -Wextra -I. -Iinclude ^
//      -o build/image_core_test.exe tests/image_core_test.cpp
//
// Ejecución: build\image_core_test.exe  (exit 0 = todos los tests OK)
//   Variante fuzz determinista: ARTPICST_TEST_FUZZ_ITERS=100000
// ============================================================================

#include "../src/image_core.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

// GetEnvironmentVariableW viene de windows.h en el SDK; se declara de forma
// mínima para compilar sin arrastrar el SDK completo. (_wtoi se evita a
// propósito: la CRT la declara inline y una redeclaración extern "C" choca.)
#ifndef _WINDOWS_
extern "C" __declspec(dllimport) unsigned long __stdcall GetEnvironmentVariableW(
    const wchar_t*, wchar_t*, unsigned long);
#endif

namespace {

inline long ParseEnvNumber(const wchar_t* s) {
    return wcstol(s, nullptr, 10);
}

int g_failures = 0;

// Mensajes de fallo: printf ESTRECHO. Los argumentos (__FILE__, #cond) son
// literales ASCII y %hs no existe en C99: MinGW con __USE_MINGW_ANSI_STDIO no
// lo interpreta de forma fiable en wprintf, mientras que printf con %s es
// idéntico en MSVC y MinGW.
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

#define CHECK_EQ(a, b)                                                     \
    do {                                                                   \
        const auto va_ = (a);                                              \
        const auto vb_ = (b);                                              \
        if (!(va_ == vb_)) {                                               \
            std::printf("[FAIL] %s:%d: %s == %s (%llu vs %llu)\n",         \
                        __FILE__, __LINE__, #a, #b,                        \
                        static_cast<unsigned long long>(va_),              \
                        static_cast<unsigned long long>(vb_));             \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

// ----------------------------------------------------------------------------
// 1. Invariantes de compilación (se re-verifican aquí por claridad)
// ----------------------------------------------------------------------------
void TestCompileTimeInvariants() {
    CHECK(artpicst::kPixelAlignment >= artpicst::kSimdAlignment);
    CHECK((artpicst::kPixelAlignment & (artpicst::kPixelAlignment - 1)) == 0);
    CHECK(artpicst::kLut.invert[0] == 255);
    CHECK(artpicst::kLut.invert[255] == 0);
    CHECK(artpicst::kLut.invert[128] == 127);
}

// ----------------------------------------------------------------------------
// 2. LUT de claridad contra la fórmula original en coma flotante
// ----------------------------------------------------------------------------
void TestClarityLutMatchesFloat() {
    for (int v = 0; v < 256; ++v) {
        // Fórmula original del visor (antes del LUT constexpr):
        //   clamp(floor(128 + (v - 128) * 1.12 + 0.5), 0, 255)
        const double exact = 128.0 + (static_cast<double>(v) - 128.0) * 1.12;
        int expected = static_cast<int>(exact + 0.5);   // floor(x+0.5) para x>=0
        if (expected < 0) expected = 0;
        if (expected > 255) expected = 255;
        const int got = artpicst::kLut.clarity[static_cast<size_t>(v)];
        if (got != expected) {
            std::wprintf(L"[FAIL] clarity[%d] = %d (esperado %d)\n", v, got, expected);
            ++g_failures;
        }
    }
}

// ----------------------------------------------------------------------------
// 3. Allocator alineado: alineación a 64 B, redondeo y realloc sin pérdidas
// ----------------------------------------------------------------------------
void TestAlignedAllocator() {
    for (size_t size : { 1u, 7u, 63u, 64u, 65u, 1000u, 4096u, 65537u }) {
        unsigned char* p = static_cast<unsigned char*>(artpicst::AlignedPixelAlloc(size));
        CHECK(p != nullptr);
        if (p) {
            CHECK_EQ(reinterpret_cast<uintptr_t>(p) % artpicst::kPixelAlignment, 0u);
            // Escribir todo el bloque: detecta buffers más pequeños de lo pedido.
            std::memset(p, 0xAB, size);
            artpicst::AlignedPixelFree(p);
        }
    }
    // realloc: conserva el prefijo y mantiene la alineación.
    unsigned char* p = static_cast<unsigned char*>(artpicst::AlignedPixelAlloc(64));
    CHECK(p != nullptr);
    if (p) {
        for (int i = 0; i < 64; ++i) p[i] = static_cast<unsigned char>(i);
        unsigned char* q = static_cast<unsigned char*>(artpicst::AlignedPixelRealloc(p, 64, 192));
        CHECK(q != nullptr);
        if (q) {
            CHECK_EQ(reinterpret_cast<uintptr_t>(q) % artpicst::kPixelAlignment, 0u);
            bool prefixOk = true;
            for (int i = 0; i < 64; ++i) prefixOk = prefixOk && (q[i] == static_cast<unsigned char>(i));
            CHECK(prefixOk);
            artpicst::AlignedPixelFree(q);
        }
    }
    // Rutas degeneradas: no deben colgarse ni corromper nada.
    CHECK(artpicst::AlignedPixelAlloc(0) == nullptr);
    artpicst::AlignedPixelFree(nullptr);
    unsigned char* fresh = static_cast<unsigned char*>(artpicst::AlignedPixelRealloc(nullptr, 0, 32));
    CHECK(fresh != nullptr);
    artpicst::AlignedPixelFree(fresh);
}

// ----------------------------------------------------------------------------
// 4. Premultiplicado alfa: referencia por bytes contra rutas SIMD
//    (Cubre tanto la ruta alineada SSE2 como la cola escalar.)
// ----------------------------------------------------------------------------
void TestPremultiply() {
    const size_t kPixels = 257;   // impar: ejercita el tail escalar
    const size_t bytes = kPixels * 4;
    std::vector<unsigned char> src(bytes);
    for (size_t i = 0; i < bytes; ++i) src[i] = static_cast<unsigned char>((i * 37 + 11) & 0xFF);
    // Mezcla de alfas: opacos, cero, intermedios (incluye el caso alfa=0).
    for (size_t i = 0; i < kPixels; ++i) {
        src[i * 4 + 3] = static_cast<unsigned char>((i % 5 == 0) ? 255 : (i % 7 == 0) ? 0 : 40 + (i % 200));
    }

    std::vector<unsigned char> simd = src;
    artpicst::PremultiplyBgra(simd.data(), kPixels);

    for (size_t i = 0; i < kPixels; ++i) {
        const unsigned char a = src[i * 4 + 3];
        const unsigned char expB = artpicst::PremulByte(src[i * 4 + 0], a);
        const unsigned char expG = artpicst::PremulByte(src[i * 4 + 1], a);
        const unsigned char expR = artpicst::PremulByte(src[i * 4 + 2], a);
        if (simd[i * 4 + 0] != expB || simd[i * 4 + 1] != expG ||
            simd[i * 4 + 2] != expR || simd[i * 4 + 3] != a) {
            std::wprintf(L"[FAIL] Premultiply píxel %zu\n", i);
            ++g_failures;
            break;
        }
    }

    // CopyPremultipliedBgra debe producir el mismo resultado.
    std::vector<unsigned char> dst(bytes, 0);
    artpicst::CopyPremultipliedBgra(dst.data(), src.data(), kPixels);
    CHECK(std::memcmp(dst.data(), simd.data(), bytes) == 0);
}

// ----------------------------------------------------------------------------
// 5. Detección de transparencia (con y sin transformación de canales)
// ----------------------------------------------------------------------------
void TestAlphaDetection() {
    const size_t kPixels = 100;
    std::vector<unsigned char> buf(kPixels * 4, 0);

    // Todo opaco: false. (Ojo: SwapRbDetectAlpha TAMBIÉN intercambia R<->B.)
    for (size_t i = 0; i < kPixels; ++i) {
        buf[i * 4 + 0] = 1; buf[i * 4 + 1] = 2; buf[i * 4 + 2] = 3; buf[i * 4 + 3] = 255;
    }
    CHECK(artpicst::DetectAnyNonOpaque(buf.data(), kPixels) == false);
    bool hasAlpha = artpicst::SwapRbDetectAlpha(buf.data(), kPixels);
    CHECK(hasAlpha == false);
    // El intercambio R<->B se aplicó: [0]=3, [2]=1 ahora.
    CHECK(buf[0] == 3 && buf[2] == 1);

    // Un solo píxel semi-transparente al final: true (recorre TODO el buffer).
    buf[(kPixels - 1) * 4 + 3] = 128;
    hasAlpha = artpicst::SwapRbDetectAlpha(buf.data(), kPixels);
    CHECK(hasAlpha == true);
    for (size_t i = 0; i < kPixels; ++i) buf[i * 4 + 3] = 255;
    CHECK(artpicst::DetectAnyNonOpaque(buf.data(), kPixels) == false);
    buf[42 * 4 + 3] = 0;
    CHECK(artpicst::DetectAnyNonOpaque(buf.data(), kPixels) == true);
}

// ----------------------------------------------------------------------------
// 6. Horneado de efectos: gris BT.601, negativo y no-op
// ----------------------------------------------------------------------------
void TestBakeEffects() {
    const size_t kPixels = 8;
    std::vector<unsigned char> src(kPixels * 4);
    std::vector<unsigned char> dst(kPixels * 4, 0);
    for (size_t i = 0; i < src.size(); ++i) src[i] = static_cast<unsigned char>(i * 7 + 3);

    // Sin efectos: copia exacta.
    CHECK(artpicst::BakeBgra(src.data(), dst.data(), kPixels, artpicst::kEffectNone));
    CHECK(std::memcmp(src.data(), dst.data(), src.size()) == 0);

    // Negativo: LUT 255-v, alfa intacto.
    CHECK(artpicst::BakeBgra(src.data(), dst.data(), kPixels, artpicst::kEffectInvert));
    bool invertOk = true;
    for (size_t i = 0; i < kPixels; ++i) {
        invertOk = invertOk
            && dst[i * 4 + 0] == artpicst::LutInvert(src[i * 4 + 0])
            && dst[i * 4 + 1] == artpicst::LutInvert(src[i * 4 + 1])
            && dst[i * 4 + 2] == artpicst::LutInvert(src[i * 4 + 2])
            && dst[i * 4 + 3] == src[i * 4 + 3];
    }
    CHECK(invertOk);

    // Gris: R, G, B == luma BT.601, alfa intacto.
    CHECK(artpicst::BakeBgra(src.data(), dst.data(), kPixels, artpicst::kEffectGray));
    bool grayOk = true;
    for (size_t i = 0; i < kPixels; ++i) {
        const unsigned char lum = artpicst::LumaBt601(src[i * 4 + 2], src[i * 4 + 1], src[i * 4 + 0]);
        grayOk = grayOk
            && dst[i * 4 + 0] == lum && dst[i * 4 + 1] == lum
            && dst[i * 4 + 2] == lum && dst[i * 4 + 3] == src[i * 4 + 3];
    }
    CHECK(grayOk);

    // Entradas nulas: false sin tocar nada.
    CHECK(artpicst::BakeBgra(nullptr, dst.data(), kPixels, artpicst::kEffectGray) == false);
    CHECK(artpicst::BakeBgra(src.data(), nullptr, kPixels, artpicst::kEffectGray) == false);
    CHECK(artpicst::BakeBgra(src.data(), dst.data(), 0, artpicst::kEffectGray) == false);
}

// ----------------------------------------------------------------------------
// 7. Transformaciones geométricas contra una referencia directa
// ----------------------------------------------------------------------------
void TestTransforms() {
    const uint32_t w = 5, h = 3;
    std::vector<unsigned char> src(w * h * 4);
    std::vector<unsigned char> dst(w * h * 4);
    std::vector<unsigned char> scratch(w * h * 4);
    // Píxel (x, y) codificado en los 4 canales.
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            unsigned char* p = src.data() + (y * w + x) * 4;
            p[0] = static_cast<unsigned char>(x);           // B
            p[1] = static_cast<unsigned char>(y);           // G
            p[2] = static_cast<unsigned char>(x * 16 + y);  // R
            p[3] = 255;
        }

    // Sin transformación: copia.
    uint32_t outW = 0, outH = 0;
    const unsigned char* ret = nullptr;
    ret = artpicst::TransformBgra(src.data(), w, h, 0, false, false, dst.data(), scratch.data(), outW, outH);
    CHECK(ret != nullptr);
    CHECK_EQ(outW, w); CHECK_EQ(outH, h);
    CHECK(std::memcmp(src.data(), dst.data(), src.size()) == 0);

    // 90° CW: dst(x', y') = src(y, h-1-x') y las dimensiones se intercambian.
    ret = artpicst::TransformBgra(src.data(), w, h, 90, false, false, dst.data(), scratch.data(), outW, outH);
    CHECK(ret != nullptr);
    CHECK_EQ(outW, h); CHECK_EQ(outH, w);
    bool rotOk = true;
    for (uint32_t x = 0; x < h; ++x)          // x recorre el ancho destino (= h)
        for (uint32_t y = 0; y < w; ++y) {    // y recorre el alto destino (= w)
            const unsigned char* d = dst.data() + (y * h + x) * 4;
            const unsigned char* s = src.data() + ((h - 1 - x) * w + y) * 4;
            rotOk = rotOk && d[0] == s[0] && d[1] == s[1] && d[2] == s[2];
        }
    CHECK(rotOk);

    // Flip H: dst(x,y) = src(w-1-x, y).
    ret = artpicst::TransformBgra(src.data(), w, h, 0, true, false, dst.data(), scratch.data(), outW, outH);
    CHECK(ret != nullptr);
    bool flipOk = true;
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const unsigned char* d = dst.data() + (y * w + x) * 4;
            const unsigned char* s = src.data() + (y * w + (w - 1 - x)) * 4;
            flipOk = flipOk && d[0] == s[0] && d[1] == s[1] && d[2] == s[2];
        }
    CHECK(flipOk);

    // 180° = flip H + flip V.
    ret = artpicst::TransformBgra(src.data(), w, h, 180, false, false, dst.data(), scratch.data(), outW, outH);
    CHECK(ret != nullptr);
    bool halfOk = true;
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const unsigned char* d = dst.data() + (y * w + x) * 4;
            const unsigned char* s = src.data() + ((h - 1 - y) * w + (w - 1 - x)) * 4;
            halfOk = halfOk && d[0] == s[0] && d[1] == s[1] && d[2] == s[2];
        }
    CHECK(halfOk);

    // Espejo + rotación combinados (requiere scratch): 90 CW + flip H.
    ret = artpicst::TransformBgra(src.data(), w, h, 90, true, false, dst.data(), scratch.data(), outW, outH);
    CHECK(ret != nullptr);
    CHECK_EQ(outW, h); CHECK_EQ(outH, w);
    // Referencia independiente: flip H primero, luego 90 CW.
    std::vector<unsigned char> tmp(w * h * 4);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x)
            std::memcpy(tmp.data() + (y * w + x) * 4,
                        src.data() + (y * w + (w - 1 - x)) * 4, 4);
    std::vector<unsigned char> ref(h * w * 4);
    for (uint32_t x = 0; x < h; ++x)
        for (uint32_t y = 0; y < w; ++y)
            std::memcpy(ref.data() + (y * h + x) * 4,
                        tmp.data() + ((h - 1 - x) * w + y) * 4, 4);
    CHECK(std::memcmp(dst.data(), ref.data(), ref.size()) == 0);
}

// ----------------------------------------------------------------------------
// 8. Baldosa de ajedrez: paridad de celdas y completado del buffer
// ----------------------------------------------------------------------------
void TestCheckerTile() {
    constexpr uint32_t kTile = 16, kCell = 8;
    std::vector<uint32_t> tile(kTile * kTile, 0);
    const uint32_t colorA = 0xFF121216u, colorB = 0xFF1A1A22u;
    artpicst::FillCheckerTileBgra(tile.data(), kTile, kCell, colorA, colorB);
    bool ok = true;
    for (uint32_t y = 0; y < kTile && ok; ++y)
        for (uint32_t x = 0; x < kTile && ok; ++x) {
            const uint32_t expected = (((x / kCell) + (y / kCell)) & 1u) ? colorB : colorA;
            ok = tile[y * kTile + x] == expected;
        }
    CHECK(ok);
    // Sin celda o sin tile: no escribe nada.
    std::vector<uint32_t> tile2(kTile * kTile, 0xAAAAAAAAu);
    artpicst::FillCheckerTileBgra(tile2.data(), kTile, 0, colorA, colorB);
    CHECK(tile2[0] == 0xAAAAAAAAu);
}

// ----------------------------------------------------------------------------
// 9. Fuzzing determinista del allocator: el visor asigna tamaños "raros"
//    (imagen 3x2, 1xN, imágenes gigantes recortadas). Propiedad: toda
//    asignación exitosa es escribible en su totalidad y liberable.
// ----------------------------------------------------------------------------
void TestAllocatorFuzz() {
    wchar_t envBuf[16] = {};
    const unsigned long n = GetEnvironmentVariableW(L"ARTPICST_TEST_FUZZ_ITERS", envBuf, 16);
    unsigned long iters = (n > 0 && n < 16) ? static_cast<unsigned long>(ParseEnvNumber(envBuf)) : 20000ul;
    if (iters == 0) iters = 20000ul;

    unsigned int rng = 0xC0FFEEu;
    auto nextRand = [&rng]() {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return rng;
    };
    for (unsigned long i = 0; i < iters; ++i) {
        const size_t size = 1 + nextRand() % 262144;   // hasta 256 KiB
        unsigned char* p = static_cast<unsigned char*>(artpicst::AlignedPixelAlloc(size));
        if (!p) continue;   // OOM del fuzz: aceptable, sigue
        if (reinterpret_cast<uintptr_t>(p) % artpicst::kPixelAlignment != 0) {
            std::wprintf(L"[FAIL] Fuzz alloc %lu: alineación rota (size=%zu)\n", i, size);
            ++g_failures;
            artpicst::AlignedPixelFree(p);
            break;
        }
        // size==1: p[0] y p[size-1] son el MISMO byte — escribir dos valores
        // distintos hace que el último gane. Solo se verifica el byte final.
        p[0] = 0x5A;
        p[size - 1] = 0xA5;
        if (p[size - 1] != 0xA5) {
            std::wprintf(L"[FAIL] Fuzz alloc %lu: buffer no escribible (size=%zu)\n", i, size);
            ++g_failures;
        }
        artpicst::AlignedPixelFree(p);
    }
}

} // namespace

int main() {
    std::wprintf(L"== ARTPICST image_core tests ==\n");
    TestCompileTimeInvariants();
    TestClarityLutMatchesFloat();
    TestAlignedAllocator();
    TestPremultiply();
    TestAlphaDetection();
    TestBakeEffects();
    TestTransforms();
    TestCheckerTile();
    TestAllocatorFuzz();

    if (g_failures == 0) {
        std::wprintf(L"[OK] image_core: todos los tests superados\n");
        return 0;
    }
    std::wprintf(L"[FAIL] image_core: %d fallos\n", g_failures);
    return 1;
}
