// ============================================================================
// version.hpp — Fuente común de verdad para installer / updater / visor
// ============================================================================
// Constantes de producto, ruta de la API de GitHub y comparación inteligente
// de versiones ("auto-59", "v1.2.3", "1.2.3-rc2"...). Solo cabecera: cada
// objetivo la compila dentro de su unidad de traducción sin dependencias.
// ============================================================================
#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cwctype>
#include <cstdlib>

namespace artpicst {

// ----------------------------------------------------------------------------
// Identidad del producto y del repositorio de releases
// ----------------------------------------------------------------------------
// kAppVersion es la FUENTE ÚNICA DE VERDAD de la versión para visor,
// instalador y updater. Mantener sincronizada con version.json y con los
// recursos VERSIONINFO (.rc) al publicar.
inline constexpr const wchar_t* kAppVersion     = L"1.2.1";
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
// Comparación de LITERALES de versión en TIEMPO DE COMPILACIÓN (usada por los
// static_assert de visor e instalador). No se usa std::wstring_view aquí: MSVC
// no pliega traits::length sobre arrays const no-constexpr (error C2131).
constexpr bool VersionStringsMatch(const wchar_t* a, const wchar_t* b) {
    while (*a != L'\0' && *a == *b) { ++a; ++b; }
    return *a == *b;   // ambos '\0' => idénticas
}

// ----------------------------------------------------------------------------
// Conversión de tags en tupla comparable
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

// ¿Tag de línea continua de CI ("auto-80", "auto-80-20260928")?
inline bool IsContinuousTag(const std::wstring& tag) {
    return tag.rfind(L"auto-", 0) == 0;
}

// Sufijo de fecha de build ("-20260928") en un tag de la línea continua.
inline bool IsDateSuffix(const std::wstring& prerelease) {
    if (prerelease.size() < 2 || prerelease[0] != L'-') return false;
    for (size_t i = 1; i < prerelease.size(); ++i)
        if (!std::iswdigit(static_cast<wint_t>(prerelease[i]))) return false;
    return true;
}

// Versión local DESCONOCIDA: marcador que usa el updater cuando ni el registro
// ni la línea de comandos aportan versión. El comparador la trata como más
// vieja que cualquier tag real de CUALQUIER línea, para que la primera
// consulta ofrezca ponerse al día en vez de callarse por el guard de líneas.
inline constexpr const wchar_t* kUnknownVersion = L"0";

// Devuelve >0 si local es más nueva, <0 si remota es más nueva, 0 si iguales.
inline int CompareVersionTags(const std::wstring& local, const std::wstring& remote) {
    if (local == remote) return 0;
    // Versión desconocida: siempre "más vieja" que un tag reconocible.
    if (local == kUnknownVersion) return -1;
    if (remote == kUnknownVersion) return 1;
    // FIX FALSOS POSITIVOS: las dos líneas de versiones son INCOMPARABLES entre
    // sí. "auto-80" vs semver "1.2.1" comparaba 1 < 80 y avisaba SIEMPRE de
    // actualizaciones aun estando en la última versión (bucle diario de
    // notificaciones falsas). Política: la línea continua (auto-N-YYYYMMDD)
    // solo se compara entre builds auto-*; la estable (v1.2.1) solo con tags
    // semver. Entre líneas: 0 (sin aviso).
    if (IsContinuousTag(local) != IsContinuousTag(remote)) return 0;
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
        // En la línea continua, el sufijo "-YYYYMMDD" es la FECHA de build del
        // mismo run: NO es pre-release, es una build más nueva (más específica)
        // que el tag sin fecha. "-rc1"/"-beta" sí siguen siendo pre-release.
        if (IsContinuousTag(local)) {
            const bool ld = IsDateSuffix(lp), rd = IsDateSuffix(rp);
            if (ld != rd) return ld ? 1 : -1;   // la fecha manda sobre sin fecha
            if (ld) return lp > rp ? 1 : -1;    // fecha vs fecha (8 dígitos)
            // ambas no-fecha (rc/beta): criterio semver de abajo
        }
        if (lp.empty()) return 1;    // release final > pre-release (semver)
        if (rp.empty()) return -1;
        return lp > rp ? 1 : -1;
    }
    return 0;
}

} // namespace artpicst
