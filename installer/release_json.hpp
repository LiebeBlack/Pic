// ============================================================================
// release_json.hpp — Parser del JSON de GitHub Releases (compartido)
// ============================================================================
// Extraído de updater/artpicst_updater.cpp para que el mismo código lo
// compile el updater, su selftest y el harness de fuzzing (tests/). Solo
// cabecera, sin dependencias de Windows-UI: necesita <string>, <vector> y
// las utilidades CRT de <cwchar>.
//
// INCLUYE los fixes de seguridad de la auditoría:
//   · Escape \u: nunca desreferencia más allá del fin de cadena.
//   · Cadenas sin cierre: el parser garantiza PROGRESO (no bucles infinitos).
//   · Búsqueda del asset acotada al bloque "assets" (no escanea todo el doc).
// ============================================================================

#ifndef ARTPICST_RELEASE_JSON_HPP
#define ARTPICST_RELEASE_JSON_HPP

#include <string>
#include <vector>
#include <cwchar>
#include <cwctype>

namespace artpicst {
namespace releasejson {

// Comparación case-insensitive PORTABLE (sin _wcsicmp, que es extensión MS y
// no está declarada cuando esta cabecera se compila sin windows.h — p. ej. en
// tests/release_json_fuzzer.cpp).
inline bool WcsIEqual(const wchar_t* a, const wchar_t* b) {
    while (*a && *b) {
        if (std::towlower(static_cast<wint_t>(*a)) != std::towlower(static_cast<wint_t>(*b))) return false;
        ++a; ++b;
    }
    return *a == L'\0' && *b == L'\0';
}

// ----------------------------------------------------------------------------
// Modelo de datos
// ----------------------------------------------------------------------------

struct GithubAsset {
    std::wstring name;
    std::wstring url;      // browser_download_url
    std::wstring digest;   // "sha256:..." si el release lo publica
    unsigned long long size = 0;
};

struct GithubRelease {
    std::wstring tag;      // tag_name (p. ej. auto-59)
    std::wstring body;     // notas de la versión
    std::vector<GithubAsset> assets;
    bool valid = false;
};

// ----------------------------------------------------------------------------
// Primitivas de parseo
// ----------------------------------------------------------------------------

inline void SkipJsonSpaces(const wchar_t*& p) {
    while (*p == L' ' || *p == L'\t' || *p == L'\r' || *p == L'\n') ++p;
}

// Parsea una cadena JSON con escapes. GARANTÍA DE PROGRESO: en el peor caso
// (cadena sin cierre, entrada truncada) consume hasta el fin del buffer y
// devuelve false sin bucles infinitos.
inline bool ParseJsonString(const wchar_t*& p, std::wstring& out) {
    SkipJsonSpaces(p);
    if (*p != L'"') return false;
    ++p;
    out.clear();
    while (*p && *p != L'"') {
        if (*p == L'\\' && p[1]) {
            ++p;
            switch (*p) {
                case L'n': out += L'\n'; break;
                case L't': out += L'\t'; break;
                case L'r': break;
                case L'"': out += L'"'; break;
                case L'\\': out += L'\\'; break;
                case L'/': out += L'/'; break;
                case L'b': case L'f': break;
                case L'u': {
                    // FIX de seguridad: comprobar que p[1..4] existen ANTES de
                    // desreferenciarlos (un `"...\" al final de una respuesta
                    // truncada leía fuera del buffer).
                    if (p[1] && p[2] && p[3] && p[4] &&
                        iswxdigit(static_cast<wint_t>(p[1])) && iswxdigit(static_cast<wint_t>(p[2])) &&
                        iswxdigit(static_cast<wint_t>(p[3])) && iswxdigit(static_cast<wint_t>(p[4]))) {
                        wchar_t hex[5] = { p[1], p[2], p[3], p[4], 0 };
                        out += static_cast<wchar_t>(wcstoul(hex, nullptr, 16));
                        p += 4;
                    }
                    break;
                }
                default: out += *p; break;
            }
            ++p;
        } else {
            out += *p++;
        }
    }
    if (*p != L'"') return false;
    ++p;
    return true;
}

inline bool MatchKey(const wchar_t*& p, const wchar_t* key) {
    SkipJsonSpaces(p);
    if (*p != L'"') return false;
    const wchar_t* save = p;
    std::wstring parsed;
    if (!ParseJsonString(p, parsed)) { p = save; return false; }
    SkipJsonSpaces(p);
    if (*p != L':') { p = save; return false; }
    ++p;
    if (parsed != key) { p = save; return false; }
    return true;
}

inline void SkipJsonContainer(const wchar_t*& p, wchar_t open, wchar_t close) {
    int depth = 0;
    bool inString = false;
    while (*p) {
        if (inString) {
            if (*p == L'\\') { ++p; if (!*p) return; ++p; continue; }
            if (*p == L'"') inString = false;
        } else {
            if (*p == L'"') inString = true;
            else if (*p == open) ++depth;
            else if (*p == close) {
                --depth;
                if (depth == 0) { ++p; return; }
            }
        }
        ++p;
    }
}

inline void SkipJsonValue(const wchar_t*& p) {
    SkipJsonSpaces(p);
    if (*p == L'"') {
        std::wstring tmp;
        ParseJsonString(p, tmp);
    } else if (*p == L'{' || *p == L'[') {
        SkipJsonContainer(p, *p, *p == L'{' ? L'}' : L']');
    } else {
        while (*p && *p != L',' && *p != L'}' && *p != L']') ++p;
    }
}

// Quita markdown simple de las notas para la notificación minimalista.
inline std::wstring CleanReleaseNotes(const std::wstring& body, int maxChars) {
    std::wstring out;
    out.reserve(body.size());
    bool lineStart = true;
    for (const wchar_t c : body) {
        if (c == L'`' || c == L'*' || c == L'#') continue;
        if (c == L'\r') continue;
        if (c == L'\n') {
            if (lineStart) continue;   // colapsa líneas vacías
            lineStart = true;
            out += L' ';
            continue;
        }
        lineStart = false;
        out += c;
    }
    while (!out.empty() && (out.back() == L' ' || out.back() == L'\t')) out.pop_back();
    if (static_cast<int>(out.size()) > maxChars) {
        out = out.substr(0, maxChars - 1) + L"…";
    }
    return out;
}

// Localiza el bloque del asset con nombre exacto (case-insensitive).
inline bool ParseAssetBlock(const wchar_t* begin, const wchar_t* end, GithubAsset& asset) {
    const wchar_t* p = begin;
    while (p < end) {
        if (MatchKey(p, L"name")) {
            std::wstring name;
            if (!ParseJsonString(p, name)) return false;
            SkipJsonSpaces(p);
            const wchar_t* q = p;
            while (q < end) {
                if (MatchKey(q, L"browser_download_url")) {
                    std::wstring url;
                    if (ParseJsonString(q, url)) asset.url = url;
                } else if (MatchKey(q, L"size")) {
                    SkipJsonSpaces(q);
                    if (*q >= L'0' && *q <= L'9') {
                        // wcstoull (C++11, <cwchar>): portable. (Antes se usaba
                        // _wcstoui64, extensión MS no disponible sin windows.h.)
                        asset.size = std::wcstoull(q, nullptr, 10);
                    }
                    SkipJsonValue(q);
                } else if (MatchKey(q, L"digest")) {
                    std::wstring digest;
                    if (ParseJsonString(q, digest)) asset.digest = digest;
                } else {
                    SkipJsonSpaces(q);
                    if (*q == L',' || *q == L'}' || *q == L']') { ++q; if (*q == L',' || *q == L'}' || *q == L']') break; continue; }
                    if (*q == L'"') { std::wstring v; ParseJsonString(q, v); continue; }
                    if (*q == L'{') { SkipJsonContainer(q, L'{', L'}'); continue; }
                    if (*q == L'[') { SkipJsonContainer(q, L'[', L']'); continue; }
                    SkipJsonValue(q);
                }
                SkipJsonSpaces(q);
                if (*q == L'}') break;
            }
            asset.name = name;
            return true;
        }
        ++p;
    }
    return false;
}

// ----------------------------------------------------------------------------
// Parseo del documento completo
// ----------------------------------------------------------------------------

inline GithubRelease ParseReleaseJson(const std::wstring& json) {
    GithubRelease release;
    const wchar_t* p = json.c_str();
    SkipJsonSpaces(p);
    if (*p != L'{') return release;

    // 1) tag_name y body en el objeto raíz.
    const wchar_t* q = p + 1;
    while (*q) {
        if (MatchKey(q, L"tag_name")) {
            if (!ParseJsonString(q, release.tag)) return release;
            continue;
        }
        if (MatchKey(q, L"body")) {
            std::wstring body;
            if (ParseJsonString(q, body)) release.body = body;
            continue;
        }
        if (*q == L'"') {
            std::wstring key;
            const wchar_t* save = q;
            if (ParseJsonString(q, key) && key == L"assets") {
                SkipJsonSpaces(q);
                if (*q == L':') { ++q; SkipJsonSpaces(q); }   // salta los ':' de "assets"
                if (*q == L'[') {
                    const wchar_t* assetsBegin = q + 1;
                    const wchar_t* scan = assetsBegin;
                    int depth = 0; bool inString = false;
                    while (*scan) {
                        if (inString) {
                            if (*scan == L'\\') { ++scan; if (!*scan) break; ++scan; continue; }
                            if (*scan == L'"') inString = false;
                        } else {
                            if (*scan == L'"') inString = true;
                            else if (*scan == L'{') ++depth;
                            else if (*scan == L'}') { --depth; if (depth == 0) { ++scan; break; } }
                        }
                        ++scan;
                    }
                    const wchar_t* assetsEnd = scan;
                    const wchar_t* it = assetsBegin;
                    while (it < assetsEnd) {
                        SkipJsonSpaces(it);
                        if (*it == L'{') {
                            const wchar_t* objBegin = it;
                            const wchar_t* probe = it + 1;
                            SkipJsonContainer(probe, L'{', L'}');
                            GithubAsset asset;
                            if (ParseAssetBlock(objBegin, probe, asset)) {
                                release.assets.push_back(asset);
                            }
                            it = probe;
                        } else {
                            ++it;
                        }
                    }
                }
                continue;
            }
            q = save;
        }
        // Valor no buscado: saltarlo con seguridad.
        SkipJsonSpaces(q);
        if (*q == L'"') {
            std::wstring v;
            const wchar_t* saveStr = q;
            ParseJsonString(q, v);
            // FIX anti-bucle infinito: con una cadena MALFORMADA (sin cierre)
            // ParseJsonString falla sin avanzar; se garantiza progreso.
            if (q == saveStr) ++q;
            continue;
        }
        if (*q == L'{') { SkipJsonContainer(q, L'{', L'}'); continue; }
        if (*q == L'[') { SkipJsonContainer(q, L'[', L']'); continue; }
        if (*q == L',' || *q == L'}') { ++q; continue; }
        if (*q == 0) break;
        ++q;
    }

    release.valid = !release.tag.empty();
    return release;
}

// Asset del instalador oficial por nombre exacto (case-insensitive).
// kInstallerAsset se pasa como parámetro para no acoplar la cabecera a
// installer/version.hpp (el fuzzing usa nombres arbitrarios).
inline const GithubAsset* FindInstallerAsset(const GithubRelease& release,
                                             const wchar_t* installerAssetName) {
    for (const auto& a : release.assets) {
        if (WcsIEqual(a.name.c_str(), installerAssetName)) return &a;
    }
    return nullptr;
}

} // namespace releasejson
} // namespace artpicst

#endif  // ARTPICST_RELEASE_JSON_HPP
