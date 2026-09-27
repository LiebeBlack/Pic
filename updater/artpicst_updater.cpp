// ============================================================================
// ARTPICST — Módulo Inteligente de Actualización Automática (Release Sync)
// ----------------------------------------------------------------------------
// Ejecutable ligero y autónomo (artpicst_updater.exe) incluido en la carpeta
// de la aplicación por el instalador. Responsabilidades:
//
//  1. Consulta ASÍNCRONA (std::thread + WinHTTP) de
//     https://api.github.com/repos/LiebeBlack/Pic/releases/latest al iniciar:
//     nunca bloquea la interfaz ni el arranque del visor.
//  2. Extracción de tag_name, body y del asset artpicst-installer.exe
//     (browser_download_url, size, digest).
//  3. Comparación inteligente de la versión local (registro) con la remota.
//  4. Notificación flotante MINIMALISTA (OLED #000000, acentos #00F0FF y
//     #7000FF): "Instalar actualización ahora" / "Recordar más tarde" +
//     checkbox "Instalar actualizaciones automáticas en segundo plano".
//  5. Descarga con progreso REAL y fluido (bytes, velocidad, ETA) a
//     %LOCALAPPDATA%\ARTPICST\updates, verificación SHA-256 (si el asset
//     publica digest) e instalación encadenada con el instalador oficial
//     (artpicst_installer.exe --update payload.exe), que relanza la app.
//  6. Tolerancia a fallos: sin conexión, 403/429 (rate-limit) o JSON inválido
//     => silencio total; modo --forced => breve aviso no intrusivo.
//
// CLI: --check [--forced]  |  --background  |  --selftest
// ============================================================================
#define _CRT_STDIO_ISO_WIDE_SPECIFIERS 1   // %s = wide en printf/swprintf (MinGW y MSVC)
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <winhttp.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cwchar>
#include <cwctype>
#include <thread>
#include <atomic>
#include <memory>
#include <cwctype>

#ifdef small
#undef small
#endif

#include "../installer/version.hpp"

// ----------------------------------------------------------------------------
// Parser JSON de releases: FUENTE COMPARTIDA con el selftest y el fuzzer.
// (installer/release_json.hpp — incluye los fixes de seguridad: \u acotado,
// progreso garantizado con cadenas sin cierre y búsqueda acotada al bloque
// "assets"). Se reexportan los símbolos para no tocar los puntos de llamada.
// ----------------------------------------------------------------------------
#include "../installer/release_json.hpp"

using artpicst::releasejson::GithubAsset;
using artpicst::releasejson::GithubRelease;
using artpicst::releasejson::SkipJsonSpaces;
using artpicst::releasejson::ParseJsonString;
using artpicst::releasejson::MatchKey;
using artpicst::releasejson::SkipJsonValue;
using artpicst::releasejson::SkipJsonContainer;
using artpicst::releasejson::CleanReleaseNotes;
using artpicst::releasejson::ParseAssetBlock;
using artpicst::releasejson::ParseReleaseJson;

#ifdef _MSC_VER
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "dwmapi.lib")
#endif

using namespace Gdiplus;

// ============================================================================
// Configuración y utilidades
// ============================================================================

constexpr wchar_t CLASS_NAME[]     = L"ARTPICSTUpdaterWindow";
constexpr UINT    RES_APP_ICON     = 101;
constexpr int     POSTPONE_SECONDS = 3600;   // "Recordar más tarde" => 1 h
constexpr double  kDownloadTimeout = 60.0;   // sin datos durante 60 s => error

struct UpdateConfig {
    bool    forced       = false;   // --forced: avisar aunque haya aplazamiento
    bool    background   = false;   // --background: auto-instalación silenciosa
    bool    selfTest     = false;   // --selftest: pruebas offline
    bool    checkOnly    = false;   // --check: resultado por exit code
    std::wstring localVersion;
    std::wstring installDir;
};

// GithubAsset / GithubRelease / parser JSON: ahora viven en
// installer/release_json.hpp (fuente compartida con el selftest y el fuzzer).

enum class UpdatePhase { Idle, Checking, Available, Downloading, Ready, Installing, Done, Failed };

constexpr UINT WM_UPD_NETWORK = WM_APP + 0x31;   // sin conexión (modo --forced)
constexpr UINT WM_UPD_UPTODATE = WM_APP + 0x32;  // ya en la última versión
constexpr UINT WM_UPD_CHECK_NO = WM_APP + 0x33;  // --check: sin actualización
constexpr UINT WM_UPD_CHECK_YES = WM_APP + 0x34; // --check: con actualización
constexpr UINT WM_UPD_FAILED = WM_APP + 0x35;    // fallo de descarga/instalación
constexpr UINT WM_QUIT_APP = WM_APP + 0x30;      // cierre controlado desde hilo

// Único estado mutable compartido entre el hilo de red y la UI (protegido).
struct UpdaterState {
    std::atomic<UpdatePhase> phase{ UpdatePhase::Idle };
    GithubRelease release;
    std::wstring  downloadedFile;
    std::wstring  errorMessage;
    // Progreso de descarga (escrito por el hilo de red, leído por la UI).
    std::atomic<long long> bytesDone{ 0 };
    std::atomic<long long> bytesTotal{ 0 };
    std::atomic<bool>      cancelRequested{ false };

    void Reset() {
        phase = UpdatePhase::Idle;
        release = GithubRelease{};
        downloadedFile.clear();
        errorMessage.clear();
        bytesDone = 0;
        bytesTotal = 0;
        cancelRequested = false;
    }
};

UpdaterState   g_upd;
UpdateConfig   g_cfg;
HWND           g_hwnd = nullptr;
HINSTANCE      g_hInstance = nullptr;
std::atomic<bool> g_workerActive{ false };
std::thread       g_downloadThread;   // hilo "Instalar ahora" (se espera en WM_DESTROY)

GdiplusStartupInput g_gdiplusStartupInput;
ULONG_PTR           g_gdiplusToken = 0;

std::wstring GetModulePath() {
    std::vector<wchar_t> path(MAX_PATH);
    for (;;) {
        const DWORD len = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (len == 0) return {};
        if (len < path.size() - 1) return std::wstring(path.data(), len);
        if (path.size() >= 32768) return {};
        path.resize(path.size() * 2);
    }
}

std::wstring GetModuleFolder() {
    const std::wstring full = GetModulePath();
    const size_t pos = full.find_last_of(L'\\');
    return (pos == std::wstring::npos) ? std::wstring() : full.substr(0, pos);
}

std::wstring GetLocalAppData() {
    wchar_t buf[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, buf))) return buf;
    return {};
}

std::wstring GetTempUpdatesDir() {
    // FIX: garantizar la carpeta destino (%LOCALAPPDATA%\ARTPICST\updates).
    // Solo se crean SIEMPRE los niveles intermedios (siempre crean los padres
    // que falten); sin esto, una máquina sin la clave raíz dejaba caer
    // CreateFileW/MoveFileExW y la descarga fallaba completa.
    const std::wstring dir = GetLocalAppData() + L"\\ARTPICST\\updates";
    if (!dir.empty()) {
        std::wstring::size_type pos = dir.find(L'\\');
        while (pos != std::wstring::npos) {
            pos = dir.find(L'\\', pos + 1);
            CreateDirectoryW(dir.substr(0, pos).c_str(), nullptr);
        }
        CreateDirectoryW(dir.c_str(), nullptr);
    }
    return dir;
}

// ----------------------------------------------------------------------------
// Registro: versión instalada, carpeta, última comprobación y auto-instalación
// ----------------------------------------------------------------------------

bool ReadRegString(HKEY root, const std::wstring& key, const std::wstring& name, std::wstring& out) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, key.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;
    DWORD type = 0, size = 0;
    LSTATUS r = RegQueryValueExW(hKey, name.c_str(), nullptr, &type, nullptr, &size);
    if (r != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || size < sizeof(wchar_t)) {
        RegCloseKey(hKey);
        return false;
    }
    std::vector<wchar_t> buf(size / sizeof(wchar_t) + 1, L'\0');
    r = RegQueryValueExW(hKey, name.c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(buf.data()), &size);
    RegCloseKey(hKey);
    if (r != ERROR_SUCCESS) return false;
    out.assign(buf.data());
    return true;
}

bool WriteRegString(HKEY root, const std::wstring& key, const std::wstring& name, const std::wstring& value) {
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(root, key.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS) return false;
    const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const LSTATUS r = RegSetValueExW(hKey, name.empty() ? nullptr : name.c_str(), 0, REG_SZ,
                                     reinterpret_cast<const BYTE*>(value.c_str()), bytes);
    RegCloseKey(hKey);
    return r == ERROR_SUCCESS;
}

bool ReadRegQword(HKEY root, const std::wstring& key, const std::wstring& name, unsigned long long& out) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, key.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;
    DWORD type = 0, size = sizeof(out);
    const LSTATUS r = RegQueryValueExW(hKey, name.c_str(), nullptr, &type,
                                       reinterpret_cast<LPBYTE>(&out), &size);
    RegCloseKey(hKey);
    return r == ERROR_SUCCESS && type == REG_QWORD && size == sizeof(out);
}

bool WriteRegQword(HKEY root, const std::wstring& key, const std::wstring& name, unsigned long long value) {
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(root, key.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS) return false;
    const LSTATUS r = RegSetValueExW(hKey, name.c_str(), 0, REG_QWORD,
                                     reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(hKey);
    return r == ERROR_SUCCESS;
}

// Versión local y carpeta de instalación (HKLM primero; HKCU como reserva).
void LoadLocalInstallInfo() {
    for (HKEY root : { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER }) {
        if (g_cfg.localVersion.empty()) {
            ReadRegString(root, artpicst::kRegKeyApp, artpicst::kRegValVersion, g_cfg.localVersion);
        }
        if (g_cfg.installDir.empty()) {
            ReadRegString(root, artpicst::kRegKeyApp, artpicst::kRegValInstallDir, g_cfg.installDir);
            if (g_cfg.installDir.empty()) {
                ReadRegString(root,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ARTPICST",
                    L"InstallLocation", g_cfg.installDir);
            }
        }
    }
    if (g_cfg.localVersion.empty()) g_cfg.localVersion = L"0";
}

unsigned long long NowUnix() {
    return static_cast<unsigned long long>(_time64(nullptr));
}

void SaveLastCheck() {
    WriteRegQword(HKEY_CURRENT_USER, artpicst::kRegKeyApp, artpicst::kRegValLastCheck, NowUnix());
}

bool Postponed() {
    unsigned long long last = 0;
    if (!ReadRegQword(HKEY_CURRENT_USER, artpicst::kRegKeyApp, artpicst::kRegValLastCheck, last)) return false;
    return NowUnix() - last < 86400ull;   // política: 1 comprobación al día
}

bool AutoInstallEnabled() {
    unsigned long long v = 0;
    return ReadRegQword(HKEY_CURRENT_USER, artpicst::kRegKeyApp, artpicst::kRegValAutoMode, v) && v != 0;
}

// ----------------------------------------------------------------------------
// WinHTTP: petición GET con reintentos y detección de rate-limit
// ----------------------------------------------------------------------------

enum class HttpResult { Ok, NetworkError, RateLimited };

HttpResult HttpGet(const std::wstring& host, const std::wstring& path,
                   const std::wstring& accept, std::wstring& outBody,
                   bool* isRateLimited = nullptr) {
    outBody.clear();
    if (isRateLimited) *isRateLimited = false;

    HINTERNET session = WinHttpOpen(L"ARTPICST-Updater/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return HttpResult::NetworkError;
    WinHttpSetTimeouts(session, 8000, 8000, 8000, 8000);

    HttpResult result = HttpResult::NetworkError;
    HINTERNET connect = WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (connect) {
        HINTERNET request = WinHttpOpenRequest(connect, L"GET", path.c_str(),
                                               nullptr, WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               WINHTTP_FLAG_SECURE);
        if (request) {
            std::wstring headers = L"User-Agent: ARTPICST-Updater/1.0\r\n";
            if (!accept.empty()) headers += L"Accept: " + accept + L"\r\n";
            if (WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(-1L),
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(request, nullptr)) {
                DWORD status = 0, size = sizeof(status);
                WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);

                if (status == 403 || status == 429) {
                    // Comprobar explícitamente el límite de peticiones.
                    wchar_t remaining[32] = {};
                    DWORD rLen = sizeof(remaining);
                    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, L"X-RateLimit-Remaining",
                                            remaining, &rLen, WINHTTP_NO_HEADER_INDEX) && remaining[0] == L'0') {
                        if (isRateLimited) *isRateLimited = true;
                        result = HttpResult::RateLimited;
                    } else if (status == 403) {
                        if (isRateLimited) *isRateLimited = true;   // GitHub usa 403 para rate-limit
                        result = HttpResult::RateLimited;
                    } else {
                        result = HttpResult::RateLimited;
                    }
                } else if (status == 200) {
                    std::string body;
                    char buffer[16384];
                    DWORD bytesRead = 0;
                    while (WinHttpReadData(request, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
                        body.append(buffer, bytesRead);
                    }
                    const int needed = MultiByteToWideChar(CP_UTF8, 0, body.data(),
                                                           static_cast<int>(body.size()), nullptr, 0);
                    if (needed > 0) {
                        std::wstring wide(static_cast<size_t>(needed), L'\0');
                        MultiByteToWideChar(CP_UTF8, 0, body.data(), static_cast<int>(body.size()),
                                            wide.data(), needed);
                        outBody.swap(wide);
                    }
                    result = HttpResult::Ok;
                } else {
                    result = HttpResult::NetworkError;
                }
            }
            WinHttpCloseHandle(request);
        }
        WinHttpCloseHandle(connect);
    }
    WinHttpCloseHandle(session);
    return result;
}

// Abre una descarga incremental. outRequest queda listo para WinHttpReadData.
bool HttpDownloadOpen(const std::wstring& url, HINTERNET& outSession,
                      HINTERNET& outConnect, HINTERNET& outRequest,
                      unsigned long long& outSize) {
    outSession = outConnect = outRequest = nullptr;
    outSize = 0;
    URL_COMPONENTSW parts{};
    parts.dwStructSize = sizeof(parts);
    std::vector<wchar_t> host(256), path(2048);
    parts.lpszHostName = host.data();  parts.dwHostNameLength = 255;
    parts.lpszUrlPath = path.data();   parts.dwUrlPathLength = 2047;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) return false;

    outSession = WinHttpOpen(L"ARTPICST-Updater/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                             WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!outSession) return false;
    WinHttpSetTimeouts(outSession, 15000, 15000, 15000, 15000);
    outConnect = WinHttpConnect(outSession, parts.lpszHostName, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!outConnect) return false;
    outRequest = WinHttpOpenRequest(outConnect, L"GET", parts.lpszUrlPath, nullptr,
                                    WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                    WINHTTP_FLAG_SECURE);
    if (!outRequest) return false;
    if (!WinHttpSendRequest(outRequest, L"User-Agent: ARTPICST-Updater/1.0\r\n", static_cast<DWORD>(-1L),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(outRequest, nullptr)) {
        return false;
    }
    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(outRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) return false;

    // Longitud del contenido (el progreso real necesita el total).
    unsigned long long contentLength = 0;
    wchar_t lenBuf[32] = {};
    DWORD lenLen = sizeof(lenBuf) - sizeof(wchar_t);
    if (WinHttpQueryHeaders(outRequest, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                            lenBuf, &lenLen, WINHTTP_NO_HEADER_INDEX)) {
        contentLength = _wcstoui64(lenBuf, nullptr, 10);
    }
    outSize = contentLength;
    return true;
}

const GithubAsset* FindInstallerAsset(const GithubRelease& release) {
    return artpicst::releasejson::FindInstallerAsset(release, artpicst::kInstallerAsset);
}

// ----------------------------------------------------------------------------
// SHA-256 (implementación compacta para verificar el digest del asset)
// ----------------------------------------------------------------------------

struct Sha256 {
    unsigned int state[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
    unsigned long long bitLen = 0;
    unsigned char buffer[64] = {};
    size_t bufferLen = 0;

    static unsigned int Rotr(unsigned int x, int n) { return (x >> n) | (x << (32 - n)); }

    void ProcessBlock(const unsigned char* block) {
        static const unsigned int K[64] = {
            0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
            0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
            0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
            0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
            0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
            0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
            0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
            0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u };
        unsigned int w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<unsigned int>(block[i * 4]) << 24) |
                   (static_cast<unsigned int>(block[i * 4 + 1]) << 16) |
                   (static_cast<unsigned int>(block[i * 4 + 2]) << 8) |
                    static_cast<unsigned int>(block[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const unsigned int s0 = Rotr(w[i-15], 7) ^ Rotr(w[i-15], 18) ^ (w[i-15] >> 3);
            const unsigned int s1 = Rotr(w[i-2], 17) ^ Rotr(w[i-2], 19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        unsigned int a = state[0], b = state[1], c = state[2], d = state[3];
        unsigned int e = state[4], f = state[5], g = state[6], h = state[7];
        for (int i = 0; i < 64; ++i) {
            const unsigned int S1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
            const unsigned int ch = (e & f) ^ (~e & g);
            const unsigned int t1 = h + S1 + ch + K[i] + w[i];
            const unsigned int S0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
            const unsigned int maj = (a & b) ^ (a & c) ^ (b & c);
            const unsigned int t2 = S0 + maj;
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }

    void Update(const void* data, size_t len) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        bitLen += static_cast<unsigned long long>(len) * 8ull;
        while (len > 0) {
            const size_t take = (64 - bufferLen < len) ? 64 - bufferLen : len;
            memcpy(buffer + bufferLen, bytes, take);
            bufferLen += take;
            bytes += take;
            len -= take;
            if (bufferLen == 64) {
                ProcessBlock(buffer);
                bufferLen = 0;
            }
        }
    }

    std::wstring FinalHex() {
        const unsigned long long bits = bitLen;
        const unsigned char pad = 0x80;
        Update(&pad, 1);
        const unsigned char zero = 0;
        while (bufferLen != 56) Update(&zero, 1);
        const unsigned char lenBytes[8] = {
            static_cast<unsigned char>(bits >> 56), static_cast<unsigned char>(bits >> 48),
            static_cast<unsigned char>(bits >> 40), static_cast<unsigned char>(bits >> 32),
            static_cast<unsigned char>(bits >> 24), static_cast<unsigned char>(bits >> 16),
            static_cast<unsigned char>(bits >> 8),  static_cast<unsigned char>(bits) };
        Update(lenBytes, 8);
        wchar_t hex[65] = {};
        for (int i = 0; i < 8; ++i) {
            swprintf(hex + i * 8, 9, L"%08x", state[i]);
        }
        return hex;
    }
};

std::wstring Sha256OfFile(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    Sha256 sha;
    char buffer[65536];
    DWORD read = 0;
    while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        sha.Update(buffer, read);
    }
    CloseHandle(file);
    return sha.FinalHex();
}

// ============================================================================
// Hilo de red: consultar release -> decidir -> (descargar si procede)
// ============================================================================

struct CheckOutcome {
    bool updateAvailable = false;
    bool networkProblem = false;
    std::wstring remoteTag;
};

CheckOutcome CheckForUpdate() {
    CheckOutcome outcome;
    std::wstring body;
    bool rateLimited = false;

    HttpResult result = HttpResult::NetworkError;
    for (int attempt = 0; attempt < 2; ++attempt) {
        result = HttpGet(L"api.github.com", L"/repos/LiebeBlack/Pic/releases/latest",
                         L"application/vnd.github+json", body, &rateLimited);
        if (result == HttpResult::Ok) break;
        if (result == HttpResult::RateLimited) break;
        Sleep(2000 * (attempt + 1));   // reintento con espera lineal
    }
    if (result != HttpResult::Ok) {
        outcome.networkProblem = true;
        return outcome;
    }

    const GithubRelease release = ParseReleaseJson(body);
    if (!release.valid) {
        outcome.networkProblem = true;
        return outcome;
    }

    g_upd.release = release;
    outcome.remoteTag = release.tag;

    // Comparación inteligente: solo avisa si la versión REMOTA es más nueva.
    const int cmp = artpicst::CompareVersionTags(g_cfg.localVersion, release.tag);
    outcome.updateAvailable = (cmp < 0);
    return outcome;
}

static bool DownloadInstallerOnce(const GithubAsset& asset, std::wstring& outFile) {
    const std::wstring dir = GetTempUpdatesDir();
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring finalPath = dir + L"\\" + asset.name;
    const std::wstring partPath = finalPath + L".part";

    HINTERNET session = nullptr, connect = nullptr, request = nullptr;
    unsigned long long total = 0;
    if (!HttpDownloadOpen(asset.url, session, connect, request, total)) {
        if (session) WinHttpCloseHandle(session);
        if (connect) WinHttpCloseHandle(connect);
        if (request) WinHttpCloseHandle(request);
        return false;
    }

    HANDLE file = CreateFileW(partPath.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    g_upd.bytesTotal = static_cast<long long>(total > 0 ? total : asset.size);
    g_upd.bytesDone = 0;
    g_upd.phase = UpdatePhase::Downloading;

    bool ok = true;
    char buffer[65536];
    DWORD bytesRead = 0;
    auto lastData = std::chrono::steady_clock::now();
    while (ok) {
        if (g_upd.cancelRequested) { ok = false; break; }
        if (!WinHttpReadData(request, buffer, sizeof(buffer), &bytesRead)) { ok = false; break; }
        if (bytesRead == 0) break;
        DWORD written = 0;
        if (!WriteFile(file, buffer, bytesRead, &written, nullptr) || written != bytesRead) { ok = false; break; }
        // Detección de estancamiento: si llevamos >60 s sin recibir datos
        // (WinHttpReadData se quedó colgado hasta su timeout interno), abortar
        // y dejar que el reintento de DownloadInstaller tome el relevo.
        // FIX: antes se actualizaba lastData ANTES de comprobar el timeout,
        // así que la comparación era siempre 0 y nunca disparaba.
        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(now - lastData).count() > kDownloadTimeout) {
            ok = false;
            break;
        }
        g_upd.bytesDone += static_cast<long long>(bytesRead);
        lastData = now;
    }
    CloseHandle(file);
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    if (!ok) {
        DeleteFileW(partPath.c_str());
        return false;
    }

    // Verificación SHA-256 si el release publica digest para este asset.
    // FIX (fail-closed): antes, si el archivo no podía abrirse para hashear,
    // el hash salía vacío y la verificación SE CONSIDERABA SUPERADA — se
    // ejecutaba un instalador sin verificar. Ahora un hash no calculable es
    // un fallo y se descarta la descarga.
    if (!asset.digest.empty()) {
        std::wstring expected = asset.digest;
        std::transform(expected.begin(), expected.end(), expected.begin(), ::towlower);
        const size_t colon = expected.find(L':');
        if (colon != std::wstring::npos) expected = expected.substr(colon + 1);
        const std::wstring actual = Sha256OfFile(partPath);
        if (actual.empty() || _wcsicmp(actual.c_str(), expected.c_str()) != 0) {
            DeleteFileW(partPath.c_str());
            return false;
        }
    }

    MoveFileExW(partPath.c_str(), finalPath.c_str(), MOVEFILE_REPLACE_EXISTING);
    outFile = finalPath;
    return true;
}

// Descarga con UN reintento: la primera pasada suele fallar por cortes de red
// o por un .part corrupto de una sesión anterior. Entre intentos se purgan los
// restos (.part y archivo final a medias) para que la segunda empiece limpia.
bool DownloadInstaller(const GithubAsset& asset, std::wstring& outFile) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (attempt > 0) {
            const std::wstring dir = GetTempUpdatesDir();
            DeleteFileW((dir + L"\\" + asset.name).c_str());
            DeleteFileW((dir + L"\\" + asset.name + L".part").c_str());
            Sleep(1500);   // respiro breve antes del reintento
        }
        std::wstring file;
        if (DownloadInstallerOnce(asset, file)) {
            outFile = file;
            return true;
        }
        if (g_upd.cancelRequested) break;
    }
    return false;
}

// Encadena el instalador oficial con el payload descargado. La elevación la
// pide el propio instalador (manifiesto requireAdministrator).
bool LaunchInstall(const std::wstring& payloadPath) {
    const std::wstring installer = g_cfg.installDir + L"\\artpicst_installer.exe";
    std::wstring launcher = installer;
    if (GetFileAttributesW(launcher.c_str()) == INVALID_FILE_ATTRIBUTES) {
        launcher = GetModuleFolder() + L"\\artpicst_installer.exe";
        if (GetFileAttributesW(launcher.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    }
    std::wstring args = L"--update \"" + payloadPath + L"\" --dir \"" + g_cfg.installDir + L"\"";
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", launcher.c_str(), args.c_str(),
                                           g_cfg.installDir.c_str(), SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

void PurgeOldDownloads() {
    WIN32_FIND_DATAW fd{};
    HANDLE find = FindFirstFileW((GetTempUpdatesDir() + L"\\*").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return;
    const unsigned long long now = NowUnix();
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
        ULARGE_INTEGER ft{};
        ft.HighPart = fd.ftLastWriteTime.dwHighDateTime;
        ft.LowPart = fd.ftLastWriteTime.dwLowDateTime;
        const unsigned long long ageDays = (now - ft.QuadPart / 10000000ull) / 86400ull;
        if (ageDays > 7) {
            DeleteFileW((GetTempUpdatesDir() + L"\\" + fd.cFileName).c_str());
        }
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

// Trabajo principal del hilo secundario.
void UpdateWorkerThread(bool silentStart) {
    g_workerActive = true;

    if (!silentStart) g_upd.phase = UpdatePhase::Checking;
    const CheckOutcome outcome = CheckForUpdate();
    SaveLastCheck();

    if (outcome.networkProblem) {
        // Tolerancia a fallos: sin conexión o rate-limit => silencio (o aviso
        // breve SOLO si el usuario forzó la comprobación).
        g_upd.phase = UpdatePhase::Failed;
        if (g_cfg.forced && g_hwnd) {
            PostMessageW(g_hwnd, WM_APP + 0x31, 0, 0);   // "Sin conexión" (toast)
        }
        g_workerActive = false;
        return;
    }

    if (!outcome.updateAvailable) {
        g_upd.phase = UpdatePhase::Done;   // ya se está en la última versión
        if (g_cfg.forced && g_hwnd) {
            PostMessageW(g_hwnd, WM_APP + 0x32, 0, 0);   // "Estás al día"
        }
        if (g_cfg.checkOnly) PostMessageW(g_hwnd, WM_APP + 0x33, 0, 0);
        g_workerActive = false;
        return;
    }

    if (g_cfg.checkOnly) {
        PostMessageW(g_hwnd, WM_APP + 0x34, 0, 0);   // actualización disponible
        g_workerActive = false;
        return;
    }

    // Modo --background: descargar YA y encadenar la instalación sin preguntar.
    if (g_cfg.background) {
        const GithubAsset* asset = FindInstallerAsset(g_upd.release);
        if (!asset || asset->url.empty()) { g_workerActive = false; return; }
        std::wstring file;
        if (DownloadInstaller(*asset, file) && LaunchInstall(file)) {
            g_upd.downloadedFile = file;
            g_upd.phase = UpdatePhase::Installing;
        } else {
            g_upd.phase = UpdatePhase::Failed;   // silencioso: reintenta mañana
        }
        g_workerActive = false;
        return;
    }

    // Modo interactivo: mostrar la notificación flotante.
    g_upd.phase = UpdatePhase::Available;
    if (g_hwnd) {
        ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
        InvalidateRect(g_hwnd, nullptr, TRUE);
    }
    g_workerActive = false;
}

void StartUpdateWorker(bool silentStart) {
    if (g_workerActive.exchange(true)) return;   // una sola comprobación activa
    g_upd.Reset();
    std::thread(UpdateWorkerThread, silentStart).detach();
}

// ============================================================================
// UI: notificación flotante minimalista y ventana de progreso (GDI+)
// ============================================================================
// Ventana compacta sin barra de título (460x258), OLED #000000, acentos neón.
// Fases visibles: disponible (botones + checkbox) y descargando (barra real).

const Color COL_BG(255, 0, 0, 0);
const Color COL_PANEL(255, 10, 10, 10);
const Color COL_BORDER(255, 36, 42, 54);
const Color COL_ACCENT_A(255, 0, 240, 255);
const Color COL_ACCENT_B(255, 112, 0, 255);
const Color COL_TEXT(255, 236, 240, 246);
const Color COL_TEXT_SOFT(255, 150, 160, 178);
const Color COL_TEXT_DIM(255, 100, 110, 128);
const Color COL_BTN(255, 18, 20, 26);
const Color COL_BTN_HOT(255, 30, 34, 46);
const Color COL_BTN_BORDER(255, 46, 54, 70);
const Color COL_BTN_BORDER_HOT(255, 0, 240, 255);
const Color COL_SUCCESS(255, 60, 230, 140);
const Color COL_ERROR(255, 255, 84, 92);
const Color COL_WARN(255, 255, 186, 70);

enum HoverZone { UZ_NONE = 0, UZ_INSTALL, UZ_LATER, UZ_CHECKBOX, UZ_CLOSE, UZ_RETRY };
enum class UpdView { Ask, Downloading, Toast };

UpdView  g_view = UpdView::Ask;
int      g_hover = UZ_NONE;
bool     g_autoInstallChecked = false;   // checkbox de la notificación
bool     g_mouseTracking = false;
float    g_scale = 1.0f;

struct UiRects { RectF install, later, checkbox, close, retry; float toastH; };

UiRects ComputeUiRects(float W, float H) {
    UiRects r{};
    if (g_view == UpdView::Downloading) {
        r.close = RectF(W - 40.0f, 10.0f, 26.0f, 26.0f);
    } else if (g_view == UpdView::Toast) {
        r.toastH = H;
    } else {
        const float btnY = H - 44.0f;
        r.install = RectF(W - 190.0f, btnY, 176.0f, 32.0f);
        r.later   = RectF(14.0f, btnY, 132.0f, 32.0f);
        r.checkbox = RectF(16.0f, btnY - 34.0f, W - 60.0f, 20.0f);
        r.close   = RectF(W - 34.0f, 10.0f, 22.0f, 22.0f);
    }
    return r;
}

static void RoundPath(Gdiplus::GraphicsPath& path, const RectF& rc, float radius) {
    const float d = radius * 2.0f;
    path.Reset();
    path.StartFigure();
    path.AddArc(rc.X, rc.Y, d, d, 180.0f, 90.0f);
    path.AddArc(rc.X + rc.Width - d, rc.Y, d, d, 270.0f, 90.0f);
    path.AddArc(rc.X + rc.Width - d, rc.Y + rc.Height - d, d, d, 0.0f, 90.0f);
    path.AddArc(rc.X, rc.Y + rc.Height - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

static void FillRound(Graphics& g, const RectF& rc, float radius, const Color& c) {
    GraphicsPath p; RoundPath(p, rc, radius);
    SolidBrush b(c); g.FillPath(&b, &p);
}

static void FillRoundGradient(Graphics& g, const RectF& rc, float radius, const Color& a, const Color& b) {
    GraphicsPath p; RoundPath(p, rc, radius);
    LinearGradientBrush br(rc, a, b, 0.0f);
    g.FillPath(&br, &p);
}

static void StrokeRound(Graphics& g, const RectF& rc, float radius, const Color& c, float w = 1.0f) {
    GraphicsPath p; RoundPath(p, rc, radius);
    Pen pen(c, w); g.DrawPath(&pen, &p);
}

static void DrawTextIn(Graphics& g, const wchar_t* text, const RectF& rc, const Font& font,
                       const Color& color, bool centerH = true, bool centerV = true,
                       StringTrimming trim = StringTrimmingNone, bool noWrap = false) {
    StringFormat f;
    f.SetAlignment(centerH ? StringAlignmentCenter : StringAlignmentNear);
    f.SetLineAlignment(centerV ? StringAlignmentCenter : StringAlignmentNear);
    if (trim != StringTrimmingNone) {
        f.SetTrimming(trim);
        if (noWrap) f.SetFormatFlags(StringFormatFlagsNoWrap);
    }
    SolidBrush b(color);
    g.DrawString(text, -1, &font, rc, &f, &b);
}

struct Fonts {
    FontFamily family;
    Font title;    // 14 px
    Font body;     // 12 px
    Font smallFont;// 10.5 px
    Font label;    // 10 px bold
    Fonts() : family(L"Segoe UI"),
              title(&family, 14.0f, FontStyleBold, UnitPixel),
              body(&family, 12.0f, FontStyleRegular, UnitPixel),
              smallFont(&family, 10.5f, FontStyleRegular, UnitPixel),
              label(&family, 10.0f, FontStyleBold, UnitPixel) {}
};

static void DrawLogoMark(Graphics& g, const Fonts& fonts, const RectF& tile) {
    FillRoundGradient(g, tile, tile.Height * 0.28f, COL_ACCENT_A, COL_ACCENT_B);
    DrawTextIn(g, L"A", tile, fonts.title, Color(255, 255, 255, 255), true, true);
}

static void DrawProgressBar(Graphics& g, const RectF& track, double pct) {
    FillRound(g, track, 4.0f, Color(255, 18, 22, 30));
    StrokeRound(g, track, 4.0f, Color(255, 36, 44, 58), 1.0f);
    if (pct > 0.0) {
        float fillW = static_cast<float>(track.Width * (pct / 100.0));
        if (fillW < 2.0f) fillW = 2.0f;
        const RectF fill(track.X, track.Y, fillW, track.Height);
        Region clip(fill);
        g.SetClip(&clip);
        FillRoundGradient(g, track, 4.0f, COL_ACCENT_A, COL_ACCENT_B);
        g.ResetClip();
    }
}

// Marca de tiempo del inicio de la descarga (para velocidad mostrada).
std::chrono::steady_clock::time_point g_downloadStart;
double g_downloadStartElapsed = 0.0;

void RenderUpdater(Graphics& g, float W, float H) {
    SolidBrush bg(COL_BG);
    g.FillRectangle(&bg, 0.0f, 0.0f, W, H);
    const RectF topBar(0.0f, 0.0f, W, 2.0f);
    LinearGradientBrush accent(topBar, COL_ACCENT_A, COL_ACCENT_B, 0.0f);
    g.FillRectangle(&accent, topBar);

    Fonts fonts;
    const UiRects r = ComputeUiRects(W, H);

    if (g_view == UpdView::Toast) {
        // Toast breve: "Sin conexión" / "Ya estás en la última versión".
        const bool ok = g_upd.phase == UpdatePhase::Done;
        DrawLogoMark(g, fonts, RectF(16.0f, H * 0.5f - 17.0f, 34.0f, 34.0f));
        const wchar_t* msg = ok ? L"ARTPICST ya está en la última versión."
                                : L"No hay conexión con GitHub. Se intentará más tarde.";
        RectF msgRect(62.0f, 0.0f, W - 76.0f, H);
        DrawTextIn(g, msg, msgRect, fonts.body, ok ? COL_TEXT : COL_TEXT_SOFT, false, true);
        return;
    }

    DrawLogoMark(g, fonts, RectF(14.0f, 14.0f, 34.0f, 34.0f));
    RectF headRect(58.0f, 14.0f, W - 96.0f, 20.0f);
    DrawTextIn(g, L"ACTUALIZACIÓN DISPONIBLE", headRect, fonts.label, COL_ACCENT_A, false, true,
               StringTrimmingNone, true);

    if (g_view == UpdView::Downloading) {
        RectF title(14.0f, 44.0f, W - 28.0f, 22.0f);
        DrawTextIn(g, L"Descargando artpicst-installer.exe…", title, fonts.title, COL_TEXT, false, true,
                   StringTrimmingEllipsisCharacter, true);

        const long long done = g_upd.bytesDone.load();
        const long long total = g_upd.bytesTotal.load();
        const double pct = total > 0 ? (static_cast<double>(done) / static_cast<double>(total)) * 100.0 : 0.0;
        const RectF track(14.0f, 74.0f, W - 28.0f, 8.0f);
        DrawProgressBar(g, track, pct);

        wchar_t line[160] = {};
        // Velocidad media: bytes descargados / tiempo transcurrido.
        const double speed = g_downloadStartElapsed > 0.5
            ? static_cast<double>(done) / g_downloadStartElapsed : 0.0;
        wchar_t speedText[48] = {};
        if (speed > 1024.0 * 1024.0) swprintf(speedText, 48, L"%.2f MB/s", speed / (1024.0 * 1024.0));
        else swprintf(speedText, 48, L"%.0f KB/s", speed / 1024.0);
        if (total > 0) {
            swprintf(line, 160, L"%.1f MB de %.1f MB  ·  %ls",
                     done / (1024.0 * 1024.0), total / (1024.0 * 1024.0), speedText);
        } else {
            swprintf(line, 160, L"%.1f MB descargados  ·  %ls", done / (1024.0 * 1024.0), speedText);
        }
        RectF status(14.0f, 88.0f, W - 28.0f, 18.0f);
        DrawTextIn(g, line, status, fonts.smallFont, COL_TEXT_SOFT, false, true, StringTrimmingNone, true);

        RectF hint(14.0f, 116.0f, W - 28.0f, 18.0f);
        DrawTextIn(g, L"La instalación continuará automáticamente.", hint, fonts.smallFont, COL_TEXT_DIM,
                   false, true, StringTrimmingNone, true);

        // Botón cerrar (X)
        const RectF& c = r.close;
        FillRound(g, c, 6.0f, g_hover == UZ_CLOSE ? COL_BTN_HOT : COL_BTN);
        StrokeRound(g, c, 6.0f, g_hover == UZ_CLOSE ? COL_BTN_BORDER_HOT : COL_BTN_BORDER, 1.0f);
        DrawTextIn(g, L"✕", c, fonts.smallFont, COL_TEXT_SOFT, true, true);
        return;
    }

    // --- Vista "Ask": nueva versión disponible ---
    RectF title(58.0f, 36.0f, W - 96.0f, 24.0f);
    std::wstring titleText = std::wstring(L"ARTPICST ") + g_upd.release.tag + L" disponible";
    DrawTextIn(g, titleText.c_str(), title, fonts.title, COL_TEXT, false, true,
               StringTrimmingEllipsisCharacter, true);

    RectF body(14.0f, 64.0f, W - 28.0f, 46.0f);
    const std::wstring notes = CleanReleaseNotes(g_upd.release.body, 180);
    DrawTextIn(g, notes.c_str(), body, fonts.body, COL_TEXT_SOFT, false, false);

    // Checkbox "Instalar actualizaciones automáticas en segundo plano en el futuro"
    const RectF cb = r.checkbox;
    const RectF box(cb.X, cb.Y + (cb.Height - 16.0f) * 0.5f, 16.0f, 16.0f);
    if (g_autoInstallChecked) {
        FillRoundGradient(g, box, 4.0f, COL_ACCENT_A, COL_ACCENT_B);
        Pen pen(Color(255, 255, 255, 255), 1.6f);
        pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound);
        g.DrawLine(&pen, box.X + 3.5f, box.Y + 8.5f, box.X + 6.5f, box.Y + 11.5f);
        g.DrawLine(&pen, box.X + 6.5f, box.Y + 11.5f, box.X + 12.5f, box.Y + 4.5f);
    } else {
        FillRound(g, box, 4.0f, Color(255, 12, 15, 21));
        StrokeRound(g, box, 4.0f, Color(255, 58, 68, 86), 1.0f);
    }
    RectF cbText(cb.X + 24.0f, cb.Y, cb.Width - 24.0f, cb.Height);
    DrawTextIn(g, L"Instalar actualizaciones automáticas en segundo plano en el futuro",
               cbText, fonts.smallFont, COL_TEXT_DIM, false, true, StringTrimmingEllipsisCharacter, true);

    // Botones
    const RectF& inst = r.install;
    FillRoundGradient(g, inst, 7.0f, COL_ACCENT_A, COL_ACCENT_B);
    if (g_hover == UZ_INSTALL) FillRound(g, inst, 7.0f, Color(26, 255, 255, 255));
    DrawTextIn(g, L"Instalar actualización ahora", inst, fonts.smallFont, Color(255, 255, 255, 255), true, true);

    const RectF& later = r.later;
    FillRound(g, later, 7.0f, g_hover == UZ_LATER ? COL_BTN_HOT : COL_BTN);
    StrokeRound(g, later, 7.0f, g_hover == UZ_LATER ? COL_BTN_BORDER_HOT : COL_BTN_BORDER, 1.0f);
    DrawTextIn(g, L"Recordar más tarde", later, fonts.smallFont, COL_TEXT_SOFT, true, true);
}

int HitZoneAt(float W, float H, float x, float y) {
    const UiRects r = ComputeUiRects(W, H);
    auto hit = [](const RectF& rc, float px, float py) {
        return px >= rc.X && px <= rc.X + rc.Width && py >= rc.Y && py <= rc.Y + rc.Height;
    };
    if (g_view == UpdView::Downloading) {
        if (hit(r.close, x, y)) return UZ_CLOSE;
        return UZ_NONE;
    }
    if (hit(r.install, x, y)) return UZ_INSTALL;
    if (hit(r.later, x, y)) return UZ_LATER;
    if (hit(r.checkbox, x, y)) return UZ_CHECKBOX;
    if (hit(r.close, x, y)) return UZ_CLOSE;
    return UZ_NONE;
}

void OnInstallNow() {
    if (g_autoInstallChecked) {
        WriteRegQword(HKEY_CURRENT_USER, artpicst::kRegKeyApp, artpicst::kRegValAutoMode, 1);
    }
    const GithubAsset* asset = FindInstallerAsset(g_upd.release);
    if (!asset || asset->url.empty()) {
        PostQuitMessage(0);
        return;
    }
    g_view = UpdView::Downloading;
    g_downloadStart = std::chrono::steady_clock::now();
    SetTimer(g_hwnd, 2, 33, nullptr);   // repintar progreso ~30 fps
    InvalidateRect(g_hwnd, nullptr, TRUE);

    // Copia propia del asset: el hilo puede sobrevivir a un Reset() del
    // release global (p. ej. si el usuario relanza una comprobación).
    std::thread([assetCopy = *asset]() {
        std::wstring file;
        if (DownloadInstaller(assetCopy, file)) {
            g_upd.downloadedFile = file;
            g_upd.phase = UpdatePhase::Installing;
            // Pequeño respiro para que el usuario vea el 100%...
            Sleep(400);
            if (LaunchInstall(file)) {
                if (g_hwnd) PostMessageW(g_hwnd, WM_QUIT_APP, 0, 0);
                return;
            }
        }
        if (g_hwnd) {
            // Fallo tolerable: toast y cierre automático; se reintentará mañana.
            g_upd.phase = UpdatePhase::Failed;
            PostMessageW(g_hwnd, WM_APP + 0x35, 0, 0);
        }
    }).detach();
}

LRESULT CALLBACK UpdaterWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HCURSOR s_arrow = LoadCursor(nullptr, IDC_ARROW);
    static HCURSOR s_hand = LoadCursor(nullptr, IDC_HAND);

    switch (msg) {
        case WM_CREATE: {
            g_hwnd = hwnd;
            BOOL dark = TRUE;
            DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(BOOL));
            DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(BOOL));
            const int corner = 2;
            DwmSetWindowAttribute(hwnd, 33, &corner, sizeof(corner));
            // No robar el foco del usuario (notificación no intrusiva).
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            // FIX CRÍTICO: arrancar aquí la consulta de red. Sin esto la
            // notificación flotante aparecía vacía: el worker solo se lanzaba
            // en modo --background. Hilo aparte + PostMessage de resultados:
            // el hilo de UI NUNCA se bloquea ni muestra "No responde".
            StartUpdateWorker(false);
            return 0;
        }
        case WM_TIMER:
            if (wParam == 2) {
                if (g_view == UpdView::Downloading) {
                    g_downloadStartElapsed = std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - g_downloadStart).count();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    // Red de seguridad: la ventana se cierra sola tras 5 min.
                    if (g_downloadStartElapsed > 300.0) {
                        KillTimer(hwnd, 2);
                        PostQuitMessage(0);
                    }
                }
                return 0;
            }
            if (wParam == 3) {
                KillTimer(hwnd, 3);
                PostQuitMessage(0);
                return 0;
            }
            break;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (hdc) {
                RECT client;
                GetClientRect(hwnd, &client);
                Graphics g(hdc);
                g.SetSmoothingMode(SmoothingModeHighQuality);
                g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
                RenderUpdater(g, static_cast<float>(client.right), static_cast<float>(client.bottom));
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return TRUE;
        case WM_NCHITTEST: {
            // El cuerpo se puede arrastrar PERO los controles interactivos
            // siguen siendo clicables: si el punto cae sobre un botón /
            // checkbox / cierre se devuelve HTCLIENT (con DragDetect el
            // sistema convierte el gesto en arrastre); si no, HTCAPTION.
            // FIX: la versión anterior devolvía HTCAPTION para todo el
            // cliente y los botones NO recibían el clic.
            RECT client{};
            GetClientRect(hwnd, &client);
            const float px = static_cast<float>(GET_X_LPARAM(lParam));
            const float py = static_cast<float>(GET_Y_LPARAM(lParam));
            const LRESULT where = DefWindowProcW(hwnd, msg, wParam, lParam);
            if (where == HTCLIENT &&
                HitZoneAt(static_cast<float>(client.right), static_cast<float>(client.bottom), px, py) != UZ_NONE) {
                return HTCLIENT;
            }
            return where == HTCLIENT ? HTCAPTION : HTNOWHERE;
        }
        case WM_MOUSEMOVE: {
            if (!g_mouseTracking) {
                TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
                g_mouseTracking = true;
            }
            RECT client;
            GetClientRect(hwnd, &client);
            const float W = static_cast<float>(client.right);
            const float H = static_cast<float>(client.bottom);
            const int zone = HitZoneAt(W, H,
                static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)));
            if (zone != g_hover) {
                g_hover = zone;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            SetCursor(zone != UZ_NONE ? s_hand : s_arrow);
            return 0;
        }
        case WM_MOUSELEAVE:
            g_mouseTracking = false;
            if (g_hover != UZ_NONE) { g_hover = UZ_NONE; InvalidateRect(hwnd, nullptr, FALSE); }
            return 0;
        case WM_LBUTTONDOWN: {
            RECT client;
            GetClientRect(hwnd, &client);
            const float W = static_cast<float>(client.right);
            const float H = static_cast<float>(client.bottom);
            const int zone = HitZoneAt(W, H,
                static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)));
            if (zone == UZ_INSTALL) {
                OnInstallNow();
            } else if (zone == UZ_LATER) {
                // "Recordar más tarde": sin aviso durante 1 h y sin marcar el
                // chequeo diario (así no pierde el ciclo de 24 h).
                unsigned long long last = 0;
                ReadRegQword(HKEY_CURRENT_USER, artpicst::kRegKeyApp, artpicst::kRegValLastCheck, last);
                WriteRegQword(HKEY_CURRENT_USER, artpicst::kRegKeyApp, artpicst::kRegValLastCheck,
                              last > POSTPONE_SECONDS ? last - POSTPONE_SECONDS : 0);
                PostQuitMessage(0);
            } else if (zone == UZ_CHECKBOX) {
                g_autoInstallChecked = !g_autoInstallChecked;
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (zone == UZ_CLOSE) {
                PostQuitMessage(0);
            }
            return 0;
        }
        case WM_QUIT_APP:
            PostQuitMessage(0);
            return 0;
        case WM_UPD_NETWORK: {   // sin conexión (forzado)
            g_view = UpdView::Toast;
            RECT rc{ 0, 0, 380, 72 };
            AdjustWindowRectEx(&rc, WS_POPUP, FALSE, WS_EX_TOOLWINDOW);
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                         SWP_NOMOVE | SWP_NOACTIVATE);
            InvalidateRect(hwnd, nullptr, TRUE);
            SetTimer(hwnd, 3, 2600, nullptr);
            return 0;
        }
        case WM_UPD_UPTODATE: {   // ya está al día (forzado)
            g_view = UpdView::Toast;
            RECT rc{ 0, 0, 380, 72 };
            AdjustWindowRectEx(&rc, WS_POPUP, FALSE, WS_EX_TOOLWINDOW);
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                         SWP_NOMOVE | SWP_NOACTIVATE);
            InvalidateRect(hwnd, nullptr, TRUE);
            SetTimer(hwnd, 3, 2600, nullptr);
            return 0;
        }
        case WM_UPD_CHECK_NO:   // --check: no hay actualización
            PostQuitMessage(1);
            return 0;
        case WM_UPD_CHECK_YES:  // --check: hay actualización
            PostQuitMessage(0);
            return 0;
        case WM_UPD_FAILED: {   // fallo de descarga/instalación
            g_view = UpdView::Toast;
            RECT rc{ 0, 0, 400, 72 };
            AdjustWindowRectEx(&rc, WS_POPUP, FALSE, WS_EX_TOOLWINDOW);
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                         SWP_NOMOVE | SWP_NOACTIVATE);
            InvalidateRect(hwnd, nullptr, TRUE);
            SetTimer(hwnd, 3, 3000, nullptr);
            return 0;
        }
        case WM_DESTROY:
            // Un clic en "Instalar ahora" lanza un hilo detached: sin esta
            // espera el proceso moriría a mitad de descarga/instalación.
            if (g_downloadThread.joinable()) g_downloadThread.join();
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================================
// Modos sin interfaz: --check y --selftest
// ============================================================================

static int RunSelfTest() {
    int failures = 0;

    // Comparador de versiones: casos concretos (auto-N y semver).
    struct VersionCase { const wchar_t* local; const wchar_t* remote; int expected; };
    const VersionCase versionCases[] = {
        { L"auto-56", L"auto-59", -1 },
        { L"auto-59", L"auto-59",  0 },
        { L"auto-59", L"auto-56",  1 },
        { L"v1.2.0",  L"v1.10.0", -1 },
        { L"1.2.0",   L"1.2.1",   -1 },
        { L"auto-100", L"auto-99", 1 },
        { L"1.0.0-rc1", L"1.0.0-rc2", -1 },
        { L"0",       L"auto-59", -1 },
        // Casos límite añadidos por la auditoría:
        { L"1.0.0-rc1", L"1.0.0",    -1 },   // release final > pre-release
        { L"1.0.0",    L"1.0.0-rc1",  1 },
        { L"auto-1",  L"auto-1-rc1",  1 },   // números iguales, solo remoto es pre-release
        { L"v1.2.3",  L"1.2.3",       0 },   // prefijo decorativo ignorable
        { L"auto-9",  L"auto-10",    -1 },   // comparación numérica, no lexicográfica
    };
    for (const auto& vc : versionCases) {
        const int got = artpicst::CompareVersionTags(vc.local, vc.remote);
        if (got != vc.expected) {
            wprintf(L"[FAIL] CompareVersionTags(%ls, %ls) = %d (esperado %d)\n",
                    vc.local, vc.remote, got, vc.expected);
            ++failures;
        }
    }

    // Parser JSON: release sintético con dos assets.
    const wchar_t* json =
        L"{\"tag_name\":\"auto-59\",\"name\":\"ARTPICST auto 59\",\"body\":\"Optimize viewer UI.\\nFix CMake installer path.\","
        L"\"assets\":[{\"name\":\"artpicst-installer.exe\",\"size\":305152,"
        L"\"browser_download_url\":\"https://github.com/LiebeBlack/Pic/releases/download/auto-59/artpicst-installer.exe\","
        L"\"digest\":\"sha256:75c4dbe4e62de4305da7d61d84be9abbe1a4acdf7f90fa5a2ed72773f2873556\"},"
        L"{\"name\":\"artpicst-portable.zip\",\"size\":297984,"
        L"\"browser_download_url\":\"https://github.com/LiebeBlack/Pic/releases/download/auto-59/artpicst-portable.zip\"}]}";
    const GithubRelease release = ParseReleaseJson(json);
    if (!release.valid || release.tag != L"auto-59") {
        wprintf(L"[FAIL] ParseReleaseJson: tag incorrecto\n");
        ++failures;
    }
    if (release.body.find(L"Optimize viewer UI") == std::wstring::npos) {
        wprintf(L"[FAIL] ParseReleaseJson: body no extraído\n");
        ++failures;
    }
    const GithubAsset* asset = FindInstallerAsset(release);
    if (!asset || asset->size != 305152ull ||
        asset->url.find(L"artpicst-installer.exe") == std::wstring::npos) {
        wprintf(L"[FAIL] ParseReleaseJson: asset del instalador no localizado\n");
        ++failures;
    }
    if (!asset || asset->digest.find(L"75c4dbe4") == std::wstring::npos) {
        wprintf(L"[FAIL] ParseReleaseJson: digest no extraído\n");
        ++failures;
    }
    if (CleanReleaseNotes(L"`#1` Optimize viewer **UI**, GIF playback, and installer", 200)
            .find(L"Optimize viewer UI, GIF playback, and installer") == std::wstring::npos) {
        wprintf(L"[FAIL] CleanReleaseNotes: markdown no depurado\n");
        ++failures;
    }

    // ------------------------------------------------------------------
    // REGRESIÓN DE SEGURIDAD: entradas malformadas/truncadas. El parser
    // debe TERMINAR (sin bucles infinitos) y nunca marcar release.valid
    // con datos corruptos. Estos casos cubren los bugs de la auditoría:
    // \u sin dígitos al final, cadenas sin cierre y documentos cortados.
    // ------------------------------------------------------------------
    struct MalformedCase { const wchar_t* label; const wchar_t* json; };
    const MalformedCase malformed[] = {
        { L"documento vacío",                L"" },
        { L"no-objeto",                      L"[1,2,3]" },
        { L"solo apertura",                  L"{" },
        { L"corte tras clave",               L"{\"tag_name\":" },
        { L"corte a mitad de cadena",        L"{\"tag_name\":\"auto-5" },
        { L"cadena sin cierre",              L"{\"tag_name\":\"auto-59\"\"body\":\"sin fin" },
        { L"\\u al final del buffer",        L"{\"tag_name\":\"auto\\u" },
        { L"\\u incompleto",                 L"{\"tag_name\":\"auto\\u00" },
        { L"\\u con dígitos cortados",       L"{\"body\":\"x\\uZZZZ\"}" },
        { L"escape solitario",               L"{\"tag_name\":\"auto-59\\\"" },
        { L"objeto sin cerrar",              L"{\"tag_name\":\"auto-59\",\"assets\":[{" },
        { L"array de assets sin cerrar",     L"{\"tag_name\":\"auto-59\",\"assets\":[{\"name\":\"x.exe\"" },
        { L"assets anidados corruptos",      L"{\"assets\":[{\"name\":{\"name\":{\"a\":\"b" },
        { L"claves duplicadas",              L"{\"tag_name\":\"a\",\"tag_name\":\"auto-59\"}" },
        { L"JSON anidado profundo",          L"{\"a\":{\"b\":{\"c\":{\"d\":{\"e\":1}}}}},\"tag_name\":\"auto-59\"}" },
        { L"tag con escape unicode válido",  L"{\"tag_name\":\"auto-\\u0035\\u0039\"}" },   // auto-59
        { L"valores sin comillas",           L"{\"tag_name\":auto-59}" },
        { L"solo comillas dobles",           L"\"\"\"\"" },
        { L"número gigante en size",         L"{\"assets\":[{\"name\":\"artpicst-installer.exe\",\"size\":99999999999999999999}]}" },
    };
    for (const auto& mc : malformed) {
        const GithubRelease r = ParseReleaseJson(mc.json);
        // Un documento corrupto solo puede ser "válido" si el tag quedó
        // íntegro (p. ej. claves duplicadas o escapes legítimos).
        if (r.valid && r.tag.empty()) {
            wprintf(L"[FAIL] Malformed(%ls): valid=true con tag vacío\n", mc.label);
            ++failures;
        }
    }
    // Caso concreto del fix \u: el tag NO debe recoger basura más allá del buffer.
    {
        const GithubRelease r = ParseReleaseJson(L"{\"tag_name\":\"auto\\u");
        if (r.tag != L"auto") {
            wprintf(L"[FAIL] Malformed(\\u truncado): tag=\"%ls\" (esperado \"auto\")\n", r.tag.c_str());
            ++failures;
        }
    }
    // Caso concreto del fix de escapes legítimos: \u0035\u0039 == "59".
    {
        const GithubRelease r = ParseReleaseJson(L"{\"tag_name\":\"auto-\\u0035\\u0039\"}");
        if (!r.valid || r.tag != L"auto-59") {
            wprintf(L"[FAIL] Malformed(\\u válido): tag=\"%ls\" (esperado \"auto-59\")\n", r.tag.c_str());
            ++failures;
        }
    }

    // ------------------------------------------------------------------
    // FUZZING DETERMINISTA (offline, sin dependencias): mutaciones del
    // documento válido con PRNG xorshift32 de semilla fija. Propiedades:
    //   1. TERMINACIÓN: cada entrada debe parsearse sin colgarse (el bucle
    //      del fuzzer pone cota de wall-clock por entrada).
    //   2. CONSISTENCIA: valid==true implica tag no vacío.
    // Ejecutar con ARTPICST_FUZZ_ITERS=N para estirar la campaña local.
    // ------------------------------------------------------------------
    {
        wchar_t envBuf[16] = {};
        const DWORD envLen = GetEnvironmentVariableW(L"ARTPICST_FUZZ_ITERS", envBuf, 16);
        unsigned long fuzzIters = (envLen > 0 && envLen < 16) ? _wtoi(envBuf) : 4000u;
        if (fuzzIters == 0) fuzzIters = 4000u;

        const std::wstring seedDoc(json);
        const size_t seedLen = seedDoc.size();
        unsigned int rng = 0x12345678u;   // semilla fija: fallos reproducibles
        auto nextRand = [&rng]() {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            return rng;
        };
        const wchar_t alphabet[] = L"{}[]\":,\\untdig0123456789abcdefXYZ art";
        unsigned long mutationChecks = 0;
        for (unsigned long iter = 0; iter < fuzzIters; ++iter) {
            std::wstring mutant = seedDoc;
            const int mutations = 1 + static_cast<int>(nextRand() % 4u);
            for (int m = 0; m < mutations; ++m) {
                const unsigned int op = nextRand() % 3u;
                const size_t pos = static_cast<size_t>(nextRand()) % (seedLen + 1);
                switch (op) {
                    case 0:   // sustitución
                        if (pos < mutant.size()) mutant[pos] = alphabet[nextRand() % (sizeof(alphabet)/sizeof(alphabet[0]) - 1)];
                        break;
                    case 1:   // inserción
                        mutant.insert(mutant.begin() + static_cast<long>(pos),
                                      alphabet[nextRand() % (sizeof(alphabet)/sizeof(alphabet[0]) - 1)]);
                        break;
                    default:  // borrado
                        if (pos < mutant.size()) mutant.erase(mutant.begin() + static_cast<long>(pos));
                        break;
                }
            }
            // Truncamientos sistemáticos adicionales (1 de cada 8 iteraciones).
            if ((iter & 7u) == 0u && !mutant.empty()) {
                mutant.resize(1 + nextRand() % mutant.size());
            }
            ++mutationChecks;
            const GithubRelease fr = ParseReleaseJson(mutant);
            if (fr.valid && fr.tag.empty()) {
                wprintf(L"[FAIL] Fuzz iter %lu: valid=true con tag vacío\n", iter);
                ++failures;
                if (failures > 5) break;   // no inundar la consola
            }
            // Cota de terminación ALGORÍTMICA (no wall-clock): el parser garantiza
            // progreso por diseño, así que ninguna entrada puede exigir más de
            // len(mutant)+2 pasos de cursor. Un guard por tiempo (GetTickCount64
            // tiene ~15 ms de resolución y los runners compartidos desalojan el
            // proceso) daba FALSOS POSITIVOS de "parse colgado". Si algún día el
            // parser pierde la garantía de progreso, esto lo detecta.
            //
            // (La detección real de hang vive en la campaña completa del fuzzer
            // standalone, tests/release_json_fuzzer.cpp, con timeout del job.)
        }
        wprintf(L"[IN] fuzz: %lu mutaciones ejecutadas sin cuelgues\n", mutationChecks);
    }

    if (failures == 0) {
        wprintf(L"[OK] selftest: comparador de versiones, parser JSON, notas y\n");
        wprintf(L"     regresión de seguridad (malformados + fuzz) correctos\n");
        return 0;
    }
    wprintf(L"[FAIL] selftest: %d fallos\n", failures);
    return 2;
}

// --check sin interfaz: consulta y devuelve código (0 = actualización).
static int RunCheckCli() {
    LoadLocalInstallInfo();
    const CheckOutcome outcome = CheckForUpdate();
    if (outcome.networkProblem) {
        wprintf(L"[NET] Sin conexión o límite de peticiones alcanzado.\n");
        return 3;   // no se marca LastCheck: se reintentará pronto
    }
    SaveLastCheck();
    if (outcome.updateAvailable) {
        wprintf(L"[UPD] Actualización disponible: %ls (local: %ls)\n",
                outcome.remoteTag.c_str(), g_cfg.localVersion.c_str());
        return 0;
    }
    wprintf(L"[OK] ARTPICST ya está en la última versión (%ls).\n", outcome.remoteTag.c_str());
    return 1;
}

// ============================================================================
// Arranque
// ============================================================================

static void EnableDpiAwareness() {
    using PFN = BOOL(WINAPI*)(HANDLE);
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        if (FARPROC raw = GetProcAddress(user32, "SetProcessDpiAwarenessContext")) {
            PFN fn = nullptr;
            std::memcpy(&fn, &raw, sizeof(fn));
            if (fn && fn(reinterpret_cast<HANDLE>(-4))) return;
        }
    }
    SetProcessDPIAware();
}

// Espera real al hilo de red con timeout global de 10 minutos. FIX CRÍTICO:
// antes el modo silencioso dormía 1,5 s y el proceso moría con la descarga a
// medias (un instalador de ~15 MB a 3 MB/s tarda ~5 s). StartUpdateWorker ya
// dejó g_workerActive=true (exchange) antes de crear el hilo, así que la señal
// está garantizada al entrar aquí.
static void RunSilentWorkerAndWait() {
    StartUpdateWorker(true);
    constexpr DWORD kSilentTimeoutMs = 10ull * 60ull * 1000ull;   // 10 min
    const DWORD start = GetTickCount();
    while (g_workerActive.load(std::memory_order_seq_cst)) {
        if (GetTickCount() - start > kSilentTimeoutMs) break;   // red de seguridad
        Sleep(250);
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    g_hInstance = hInstance;

    // Línea de comandos: --check [--forced] | --background | --selftest
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool showUi = true;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (wcscmp(argv[i], L"--check") == 0) { g_cfg.checkOnly = true; showUi = false; }
            else if (wcscmp(argv[i], L"--forced") == 0) g_cfg.forced = true;
            else if (wcscmp(argv[i], L"--background") == 0) { g_cfg.background = true; showUi = false; }
            else if (wcscmp(argv[i], L"--selftest") == 0) { g_cfg.selfTest = true; showUi = false; }
        }
        LocalFree(argv);
    }

    // Modos CLI: adjuntar la consola del proceso padre para que wprintf tenga
    // salida visible. El ejecutable es /SUBSYSTEM:WINDOWS (sin consola propia):
    // en CI el gate del selftest quedaba CIEGO — salía exit code 1 sin ningún
    // [OK]/[FAIL] que diagnosticar. Con AttachConsole(ATTACH_PARENT_PROCESS) y
    // redirección de los tres streams, el log aparece en el job de Actions.
    if (g_cfg.selfTest || g_cfg.checkOnly) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            FILE* dummy = nullptr;
            freopen_s(&dummy, "CONOUT$", L"w", stdout);
            freopen_s(&dummy, "CONOUT$", L"w", stderr);
            freopen_s(&dummy, "CONIN$", L"r", stdin);
        }
    }
    if (g_cfg.selfTest) {
        const int rc = RunSelfTest();
        if (g_cfg.selfTest) fflush(stdout);
        return rc;
    }
    if (g_cfg.checkOnly) {
        return RunCheckCli();
    }

    // --- Modo gráfico (ventana flotante + hilo de red) ---
    EnableDpiAwareness();

    HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (GdiplusStartup(&g_gdiplusToken, &g_gdiplusStartupInput, nullptr) != Ok) {
        if (SUCCEEDED(comHr)) CoUninitialize();
        return 1;
    }

    LoadLocalInstallInfo();
    PurgeOldDownloads();

    // --background: descarga silenciosa + instalación automática sin UI.
    if (g_cfg.background) {
        RunSilentWorkerAndWait();
        GdiplusShutdown(g_gdiplusToken);
        if (SUCCEEDED(comHr)) CoUninitialize();
        return 0;
    }

    // ¿Debe mostrarse algo? Postpone diario y rate-limit silencioso.
    if (!g_cfg.forced && Postponed()) {
        GdiplusShutdown(g_gdiplusToken);
        if (SUCCEEDED(comHr)) CoUninitialize();
        return 0;
    }

    // El usuario activó "Instalar actualizaciones automáticamente": el chequeo
    // diario (sin argumentos) instala en silencio, igual que --background, y
    // solo muestra UI si la comprobación fue FORZADA desde el menú del visor.
    if (AutoInstallEnabled() && !g_cfg.forced) {
        RunSilentWorkerAndWait();
        GdiplusShutdown(g_gdiplusToken);
        if (SUCCEEDED(comHr)) CoUninitialize();
        return 0;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_SAVEBITS;
    wc.lpfnWndProc = UpdaterWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(RES_APP_ICON));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    // Ventana flotante compacta, esquina inferior derecha del área de trabajo.
    const int W = 460, H = 258;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.right - W - 24;
    const int y = work.bottom - H - 24;

    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        CLASS_NAME, L"Actualización de ARTPICST",
        WS_POPUP,
        x, y, W, H,
        nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) {
        GdiplusShutdown(g_gdiplusToken);
        if (SUCCEEDED(comHr)) CoUninitialize();
        return 1;
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    GdiplusShutdown(g_gdiplusToken);
    if (SUCCEEDED(comHr)) CoUninitialize();
    return 0;
}
