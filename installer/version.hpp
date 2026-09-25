// ============================================================================
// version.hpp — Fuente común de verdad para installer / updater / visor
// ============================================================================
// Constantes de producto, ruta de la API de GitHub y comparación inteligente
// de versiones ("auto-59", "v1.2.3", "1.2.3-rc2"...). Solo cabecera: cada
// objetivo la compila dentro de su unidad de traducción sin dependencias.
// ============================================================================
#pragma once

#include <string>
#include <vector>
#include <cwctype>
#include <cstdlib>

namespace artpicst {

// ----------------------------------------------------------------------------
// Identidad del producto y del repositorio de releases
// ----------------------------------------------------------------------------
inline constexpr const wchar_t* kAppName        = L"ARTPICST";
inline constexpr const wchar_t* kRepoOwner      = L"LiebeBlack";
inline constexpr const wchar_t* kRepoName       = L"Pic";
inline constexpr const wchar_t* kRepoUrl        = L"https://github.com/LiebeBlack/Pic";
inline constexpr const char*    kApiLatestUrlA  = "https://api.github.com/repos/LiebeBlack/Pic/releases/latest";
inline constexpr const wchar_t* kInstallerAsset = L"artpicst-installer.exe";

// Raíz del registro (se consulta HKLM primero, luego HKCU como reserva).
inline constexpr const wchar_t* kRegKeyApp       = L"Software\\ARTPICST";
inline constexpr const wchar_t* kRegValVersion   = L"Version";
inline constexpr const wchar_t* kRegValInstallDir= L"InstallDir";
inline constexpr const wchar_t* kRegValLastCheck = L"LastUpdateCheck";
inline constexpr const wchar_t* kRegValAutoMode  = L"AutoInstallUpdates";

// ----------------------------------------------------------------------------
// Comparación de versiones inteligente
// ----------------------------------------------------------------------------
// Convierte "auto-59", "v1.2.3", "1.2.3-rc2" en una tupla comparable:
//   · prefijo alfabético ("v", "auto")     -> se ignora, es decorativo
//   · secuencia de números (59 / 1.2.3)    -> componente principal (numérica:
//     "auto-100" > "auto-99", no "1" < "9")
//   · sufijo pre-release ("-rc2", "-beta") -> componente secundario lexicográ-
//     fico; solo desempata si los números coinciden (semver: 1.0.0 > 1.0.0-rc1)
inline std::vector<long long> ParseVersionNumbers(const std::wstring& tag, std::wstring& prerelease) {
    prerelease.clear();

    // Todo lo anterior al primer dígito ("v", "auto"...) es prefijo decorativo.
    size_t start = 0;
    while (start < tag.size() && !std::iswdigit(static_cast<wint_t>(tag[start]))) ++start;
    if (start == tag.size()) return {};   // sin números: nada comparable

    // El sufijo pre-release/build empieza en el primer '-' o '+' que aparece
    // DESPUÉS del primer dígito (así "auto-100" no se corta en su propio guion).
    size_t cut = tag.size();
    for (size_t i = start + 1; i < tag.size(); ++i) {
        if (tag[i] == L'-' || tag[i] == L'+') { cut = i; break; }
    }

    // Los dígitos son la secuencia principal; cualquier otro carácter separa.
    std::vector<long long> numbers;
    long long current = -1;
    for (size_t i = start; i < cut; ++i) {
        const wchar_t c = tag[i];
        if (std::iswdigit(static_cast<wint_t>(c))) {
            current = (current < 0 ? 0 : current) * 10 + (c - L'0');
        } else if (current >= 0) {
            numbers.push_back(current);
            current = -1;
        }
    }
    if (current >= 0) numbers.push_back(current);
    if (cut < tag.size()) prerelease = tag.substr(cut);
    return numbers;
}

// Devuelve >0 si local es más nueva, <0 si remota es más nueva, 0 si iguales.
inline int CompareVersionTags(const std::wstring& local, const std::wstring& remote) {
    if (local == remote) return 0;
    std::wstring lp, rp;
    const std::vector<long long> ln = ParseVersionNumbers(local, lp);
    const std::vector<long long> rn = ParseVersionNumbers(remote, rp);
    const size_t n = ln.size() > rn.size() ? ln.size() : rn.size();
    for (size_t i = 0; i < n; ++i) {
        const long long a = i < ln.size() ? ln[i] : 0;
        const long long b = i < rn.size() ? rn[i] : 0;
        if (a != b) return a > b ? 1 : -1;
    }
    if (lp != rp) {
        if (lp.empty()) return 1;    // release final > pre-release (semver)
        if (rp.empty()) return -1;
        return lp > rp ? 1 : -1;
    }
    return 0;
}

} // namespace artpicst
