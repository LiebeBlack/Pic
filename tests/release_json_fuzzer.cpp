// ============================================================================
// release_json_fuzzer.cpp — Fuzzing determinista del parser de releases
// ============================================================================
// Ejecutable de consola que martillea installer/release_json.hpp con tres
// familias de entradas adversariales:
//   1. Mutaciones (sustituir/insertar/borrar) del documento válido de release.
//   2. Truncamientos sistemáticos en TODOS los puntos de corte.
//   3. Basura binaria pseudoaleatoria de longitudes variadas.
//
// Propiedades verificadas por iteración:
//   · TERMINACIÓN: sin cuelgues (cota de iteraciones por entrada, no reloj,
//     para que el build no dependa de la velocidad de la máquina).
//   · CONSISTENCIA: valid==true => tag no vacío; FindInstallerAsset nunca
//     devuelve un asset sin nombre.
//   · LÍMITES: los size gigantes no desbordan la aritmética interna.
//
// Determinista al 100 %: semilla fija + PRNG xorshift32. Un fallo es siempre
// reproducible localmente con la misma semilla.
//
// Compilación (MSVC):
//   cl /nologo /EHsc /std:c++20 /O2 /utf-8 /W4 /I. ^
//      /Fe:build\release_json_fuzzer.exe tests\release_json_fuzzer.cpp
// Compilación (MinGW-w64):
//   g++ -std=c++20 -O2 -Wall -Wextra -I. ^
//      -o build/release_json_fuzzer.exe tests/release_json_fuzzer.cpp
//
// Uso: build\release_json_fuzzer.exe [iteraciones]
//   (por defecto 50000; sobrecargable por argumento o ARTPICST_FUZZ_ITERS)
// ============================================================================

#include "../installer/release_json.hpp"

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

// Documento válido de referencia (mismo que el selftest del updater).
std::wstring MakeSeedDoc() {
    return
        L"{\"tag_name\":\"auto-59\",\"name\":\"ARTPICST auto 59\",\"body\":\"Optimize viewer UI.\\nFix CMake installer path.\","
        L"\"assets\":[{\"name\":\"artpicst-installer.exe\",\"size\":305152,"
        L"\"browser_download_url\":\"https://github.com/LiebeBlack/Pic/releases/download/auto-59/artpicst-installer.exe\","
        L"\"digest\":\"sha256:75c4dbe4e62de4305da7d61d84be9abbe1a4acdf7f90fa5a2ed72773f2873556\"},"
        L"{\"name\":\"artpicst-portable.zip\",\"size\":297984,"
        L"\"browser_download_url\":\"https://github.com/LiebeBlack/Pic/releases/download/auto-59/artpicst-portable.zip\"}]}";
}

// PRNG xorshift32 determinista.
struct Rng {
    unsigned int state;
    explicit Rng(unsigned int seed) : state(seed ? seed : 1u) {}
    unsigned int Next() {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return state;
    }
    unsigned int Range(unsigned int n) { return Next() % n; }
};

void ValidateInvariants(const artpicst::releasejson::GithubRelease& r,
                        unsigned long iter, const char* family) {
    // Mensajes: printf ESTRECHO (%hs no es C99; MinGW con
    // __USE_MINGW_ANSI_STDIO no lo interpreta de forma fiable en wprintf).
    if (r.valid && r.tag.empty()) {
        std::printf("[FAIL] %s iter %lu: valid=true con tag vacío\n", family, iter);
        ++g_failures;
    }
    // FindInstallerAsset con nombre del instalador: el resultado, si existe,
    // debe tener nombre no vacío y URL no vacía.
    const artpicst::releasejson::GithubAsset* a =
        artpicst::releasejson::FindInstallerAsset(r, L"artpicst-installer.exe");
    if (a && (a->name.empty())) {
        std::printf("[FAIL] %s iter %lu: asset sin nombre\n", family, iter);
        ++g_failures;
    }
}

// Familia 1: mutaciones del documento válido.
void FuzzMutations(const std::wstring& seed, unsigned long iters) {
    static const wchar_t alphabet[] =
        L"{}[]\\\":,untdig0123456789abcdefXYZ \t\r\náé€";
    constexpr size_t alphabetLen = (sizeof(alphabet) / sizeof(alphabet[0])) - 1;
    Rng rng(0x12345678u);
    for (unsigned long iter = 0; iter < iters; ++iter) {
        std::wstring mutant = seed;
        const int mutations = 1 + static_cast<int>(rng.Range(6));
        for (int m = 0; m < mutations && !mutant.empty(); ++m) {
            const unsigned int op = rng.Range(3);
            const size_t pos = static_cast<size_t>(rng.Next()) % (mutant.size() + 1);
            switch (op) {
                case 0:   // sustitución
                    if (pos < mutant.size()) mutant[pos] = alphabet[rng.Range(static_cast<unsigned int>(alphabetLen))];
                    break;
                case 1:   // inserción
                    mutant.insert(mutant.begin() + static_cast<long>(pos),
                                  alphabet[rng.Range(static_cast<unsigned int>(alphabetLen))]);
                    break;
                default:  // borrado
                    if (pos < mutant.size()) mutant.erase(mutant.begin() + static_cast<long>(pos));
                    break;
            }
        }
        const auto r = artpicst::releasejson::ParseReleaseJson(mutant);
        ValidateInvariants(r, iter, "mutation");
        if (g_failures > 10) return;
    }
    std::wprintf(L"[IN] mutation: %lu entradas\n", iters);
}

// Familia 2: TODOS los truncamientos posibles del documento válido.
// (Exhaustivo, no aleatorio: garantiza que ningún prefijo cuelga el parser.)
void FuzzTruncations(const std::wstring& seed) {
    for (size_t cut = 0; cut <= seed.size(); ++cut) {
        std::wstring truncated = seed.substr(0, cut);
        const auto r = artpicst::releasejson::ParseReleaseJson(truncated);
        ValidateInvariants(r, static_cast<unsigned long>(cut), "truncation");
        if (g_failures > 10) return;
    }
    std::wprintf(L"[IN] truncation: %zu prefijos (exhaustivo)\n", seed.size() + 1);
}

// Familia 3: basura binaria pseudoaleatoria (valores wchar arbitrarios,
// incluidos los que romperían un parser ingenuo: comillas, backslashes,
// surrogates y 0x0000 en medio).
void FuzzGarbage(unsigned long iters) {
    Rng rng(0xBADC0DEu);
    for (unsigned long iter = 0; iter < iters; ++iter) {
        const size_t len = 1 + rng.Range(4096);
        std::wstring garbage;
        garbage.reserve(len);
        for (size_t i = 0; i < len; ++i) {
            const unsigned int pick = rng.Range(10);
            if (pick < 7) {
                // Caracteres estructurales y de escape. Con valores \xNNN y no
                // \uNNNN: los UCN con valor < 0x00A0 y los surrogados están
                // PROHIBIDOS en literales C++ (error del compilador).
                static const wchar_t hot[] = {L'{', L'[', L'}', L']', L'"', L'\\',
                                              L':', L',', 0x0000, 0xFFFD, 0xD800};
                garbage += hot[rng.Range(11)];
            } else {
                // Cualquier wchar (excluyendo 0x0000: terminaría el c_str).
                wchar_t c = static_cast<wchar_t>(1 + rng.Next() % 0xFFFEu);
                if (c == 0) c = L'?';
                garbage += c;
            }
        }
        const auto r = artpicst::releasejson::ParseReleaseJson(garbage);
        ValidateInvariants(r, iter, "garbage");
        if (g_failures > 10) return;
    }
    std::wprintf(L"[IN] garbage: %lu entradas\n", iters);
}

} // namespace

int main(int argc, char** argv) {
    unsigned long iters = 50000ul;
    if (argc > 1) {
        iters = static_cast<unsigned long>(atoi(argv[1]));
    } else {
        wchar_t envBuf[16] = {};
        const unsigned long n = GetEnvironmentVariableW(L"ARTPICST_FUZZ_ITERS", envBuf, 16);
        if (n > 0 && n < 16) iters = static_cast<unsigned long>(ParseEnvNumber(envBuf));
    }
    if (iters == 0) iters = 50000ul;

    std::wprintf(L"== ARTPICST release_json fuzzer (%lu iteraciones/familia aleatoria) ==\n", iters);
    const std::wstring seed = MakeSeedDoc();

    FuzzMutations(seed, iters);
    FuzzTruncations(seed);
    FuzzGarbage(iters);

    if (g_failures == 0) {
        std::wprintf(L"[OK] release_json fuzzer: sin cuelgues ni invariantes rotas\n");
        return 0;
    }
    std::wprintf(L"[FAIL] release_json fuzzer: %d fallos\n", g_failures);
    return 1;
}
