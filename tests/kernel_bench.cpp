// ============================================================================
// kernel_bench.cpp — Banco de medida de los kernels del núcleo de imagen
// ----------------------------------------------------------------------------
// Mide, con QueryPerformanceCounter, el coste REAL de cada kernel de
// src/image_core.hpp sobre buffers alineados a 64 B (la ruta vectorizada, no la
// cola escalar). Sirve para justificar y comparar el baseline SIMD: se ejecuta
// una vez sobre un binario compilado con /arch:SSE2 y otra con /arch:SSE4.2, y
// las cifras se archivan en docs/PERFORMANCE.md.
//
// Es informativo: SIEMPRE sale con 0 salvo fallo de asignación (1). No es un
// test de corrección — para eso está image_core_test.exe.
//
// Compilación (MSVC):
//   cl /nologo /EHsc /std:c++20 /O2 /arch:SSE4.2 /DARTPICST_TARGET_SSE42=1 ^
//      /utf-8 /W4 /permissive- /I. /Iinclude ^
//      /Fe:build\kernel_bench.exe tests\kernel_bench.cpp /link kernel32.lib
// MinGW:
//   g++ -std=c++20 -O2 -msse4.2 -Wall -Wextra -I. -Iinclude ^
//      -o build/kernel_bench.exe tests/kernel_bench.cpp
//
// Uso:
//   build\kernel_bench.exe                 # 2 097 152 píxeles, 30 repeticiones
//   build\kernel_bench.exe 4194304 50      # píxeles y repeticiones explícitos
// ============================================================================

#include "../src/image_core.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <chrono>
#endif

namespace {

// ----------------------------------------------------------------------------
// Reloj monótono de alta resolución
// ----------------------------------------------------------------------------
#ifdef _WIN32
double NowSeconds() {
    static LARGE_INTEGER freq{};
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER t{};
    QueryPerformanceCounter(&t);
    return static_cast<double>(t.QuadPart) / static_cast<double>(freq.QuadPart);
}
#else
double NowSeconds() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}
#endif

// Impide que el optimizador elimine un kernel cuyo resultado no se lee.
volatile unsigned long long g_sink = 0;

struct Result {
    const char* name;
    double nsPerPixel;
    double gigaBytesPerSecond;
};

Result g_results[16];
int g_resultCount = 0;

void Report(const char* name, size_t pixels, size_t bytesTouched, double seconds) {
    const double perPixel = pixels > 0 ? (seconds * 1e9) / static_cast<double>(pixels) : 0.0;
    const double gbs = seconds > 0.0
        ? (static_cast<double>(bytesTouched) / (1024.0 * 1024.0 * 1024.0)) / seconds
        : 0.0;
    if (g_resultCount < 16) g_results[g_resultCount++] = {name, perPixel, gbs};
    std::printf("  %-34s %8.3f ns/px   %8.2f GiB/s\n", name, perPixel, gbs);
}

// Ejecuta `fn` `reps` veces midiendo el tiempo total y quedándose con la mejor
// pasada (la menos contaminada por el planificador del SO).
template <typename Fn>
void Bench(const char* name, size_t pixels, size_t bytesTouched, int reps, Fn&& fn) {
    double best = 0.0;
    for (int r = 0; r < reps; ++r) {
        const double t0 = NowSeconds();
        fn();
        const double dt = NowSeconds() - t0;
        if (best == 0.0 || dt < best) best = dt;
    }
    Report(name, pixels, bytesTouched, best);
}

}  // namespace

int main(int argc, char** argv) {
    size_t pixels = 2097152;   // 2 097 152 px = 8 MiB por buffer
    int reps = 30;
    if (argc > 1) {
        const long long v = std::atoll(argv[1]);
        if (v > 0) pixels = static_cast<size_t>(v);
    }
    if (argc > 2) {
        const int v = std::atoi(argv[2]);
        if (v > 0) reps = v;
    }
    // La cola escalar debe quedar vacía para que la cifra describa SOLO la ruta
    // vectorizada: se redondea a múltiplo de 8 píxeles.
    pixels = (pixels / 8u) * 8u;
    if (pixels == 0) pixels = 8;
    const size_t bytes = pixels * 4u;

    unsigned char* a = static_cast<unsigned char*>(artpicst::AlignedPixelAlloc(bytes));
    unsigned char* b = static_cast<unsigned char*>(artpicst::AlignedPixelAlloc(bytes));
    unsigned int* tile = static_cast<unsigned int*>(artpicst::AlignedPixelAlloc(64u * 64u * 4u));
    if (!a || !b || !tile) {
        std::printf("[FAIL] no se pudo reservar la memoria del banco (%zu B)\n", bytes);
        artpicst::AlignedPixelFree(a);
        artpicst::AlignedPixelFree(b);
        artpicst::AlignedPixelFree(tile);
        return 1;
    }

    // Datos deterministas y dependientes del índice: el kernel de premultiplicado
    // no tiene ramas por píxel, así que el patrón no sesga la medida.
    for (size_t i = 0; i < bytes; ++i) a[i] = static_cast<unsigned char>((i * 131 + 7) & 0xFF);
    for (size_t i = 0; i < pixels; ++i) a[i * 4 + 3] = static_cast<unsigned char>(40 + (i % 200));

    std::printf("== ARTPICST kernel_bench ==\n");
    std::printf("SIMD: %s (nivel %d) · %zu píxeles (%.2f MiB/buffer) · %d repeticiones\n",
                artpicst::SimdLevelName(), artpicst::kSimdLevel,
                pixels, static_cast<double>(bytes) / (1024.0 * 1024.0), reps);
    std::printf("Alineacion a %zu B: %s\n\n",
                static_cast<size_t>(artpicst::kPixelAlignment),
                (artpicst::IsAligned(a, artpicst::kPixelAlignment) &&
                 artpicst::IsAligned(b, artpicst::kPixelAlignment)) ? "OK (ruta vectorizada)" : "NO");
    std::printf("%-36s %-14s %s\n", "kernel", "coste", "ancho de banda");

    // --- Intercambio B<->R + deteccion de alfa (en sitio: 2 pasadas de memoria) --
    Bench("SwapRbDetectAlpha (in situ)", pixels, bytes * 2u, reps, [&] {
        std::memcpy(a, b, bytes);           // reset fuera de la ruta del kernel
        g_sink = g_sink + (artpicst::SwapRbDetectAlpha(a, pixels) ? 1ull : 0ull);
    });

    // --- Deteccion de transparencia (solo lectura: 1 pasada) --------------------
    Bench("DetectAnyNonOpaque (solo lectura)", pixels, bytes, reps, [&] {
        g_sink = g_sink + (artpicst::DetectAnyNonOpaque(a, pixels) ? 1ull : 0ull);
    });

    // --- Premultiplicado en sitio (lee y escribe: 2 pasadas) --------------------
    Bench("PremultiplyBgra (in situ)", pixels, bytes * 2u, reps, [&] {
        std::memcpy(a, b, bytes);
        artpicst::PremultiplyBgra(a, pixels);
    });

    // --- Copia + premultiplicado fusionados (lee + escribe: 2 pasadas) ----------
    Bench("CopyPremultipliedBgra", pixels, bytes * 2u, reps, [&] {
        artpicst::CopyPremultipliedBgra(a, b, pixels);
    });

    // --- Horneado de efectos ---------------------------------------------------
    Bench("BakeBgra (sin efecto = memcpy)", pixels, bytes * 2u, reps, [&] {
        artpicst::BakeBgra(b, a, pixels, artpicst::kEffectNone);
    });
    Bench("BakeBgra (escala de grises)", pixels, bytes * 2u, reps, [&] {
        artpicst::BakeBgra(b, a, pixels, artpicst::kEffectGray);
    });
    Bench("BakeBgra (negativo, LUT)", pixels, bytes * 2u, reps, [&] {
        artpicst::BakeBgra(b, a, pixels, artpicst::kEffectInvert);
    });
    Bench("BakeBgra (ultra-claridad, LUT)", pixels, bytes * 2u, reps, [&] {
        artpicst::BakeBgra(b, a, pixels, artpicst::kEffectClarity);
    });

    // --- Transformaciones geometricas -----------------------------------------
    // Rotacion 90: el recorrido es por columnas, así que el coste refleja la
    // localidad de caché, no el ancho del registro. Se mide para vigilar
    // regresiones, no para comparar toolchains.
    const uint32_t side = 1024;   // 1024x1024 = 1 048 576 px
    const size_t sqPixels = static_cast<size_t>(side) * side;
    if (sqPixels * 4u <= bytes) {
        Bench("RotateBgra 90 grados (1024x1024)", sqPixels, sqPixels * 4u * 2u, reps, [&] {
            artpicst::RotateBgra(a, b, side, side, artpicst::Rotation::Cw90);
        });
    }

    // --- Baldosa de ajedrez (fuera de la ruta de fotograma) -------------------
    Bench("FillCheckerTileBgra (64x64)", 64u * 64u, 64u * 64u * 4u, reps, [&] {
        artpicst::FillCheckerTileBgra(tile, 64u, 8u, 0xFF202020u, 0xFF303030u);
    });

    std::printf("\nSumidero (evita que el optimizador elimine kernels): %llu\n",
                static_cast<unsigned long long>(g_sink));

    artpicst::AlignedPixelFree(a);
    artpicst::AlignedPixelFree(b);
    artpicst::AlignedPixelFree(tile);
    return 0;
}
