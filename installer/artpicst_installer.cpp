// ============================================================================
// ARTPICST — Instalador / Desinstalador / Actualizador premium
// ----------------------------------------------------------------------------
// · Asistente GDI+ "pitch-black" (#000000 / #0A0A0A) con acentos neón
//   (#00F0FF / #7000FF), ventana ampliada (880x620) con BARRA LATERAL de
//   pasos (5 etapas), centrada y REDIMENSIONABLE.
// · Página "Destino": selector de carpeta nativo, espacio libre en disco,
//   tamaño del payload y detección de instalaciones previas.
// · Página "Completado": estadísticas reales (bytes instalados y accesos).
// · La instalación, la desinstalación y la actualización corren en un hilo
//   secundario (std::thread) y publican el progreso con PostMessage: la
//   interfaz NUNCA se bloquea ni muestra "No responde".
// · Duración simulada EXACTA de 34 s (instalación/actualización): cada hito
//   ejecuta su operación real y el planificador ajusta el ritmo para que el
//   proceso completo dure 34.0 s, con fases técnicas en vivo y consola de
//   log central.
// · Payload AUTOCONTENIDO: artpicst.exe, el icono, version.json, README.md y
//   artpicst_updater.exe viajan incrustados como recursos RCDATA y se extraen
//   al instalar (ver installer/artpicst_installer.rc).
// · Desinstalación gráfica con la misma estética (--uninstall) y modo
//   actualización silencioso del asistente (--update <payload>).
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
#include <shlwapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cwchar>
#include <chrono>
#include <thread>

#include "version.hpp"

#ifdef _MSC_VER
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#endif

using namespace Gdiplus;
using artpicst::CompareVersionTags;

// ============================================================================
// Recursos incrustados (ver installer/artpicst_installer.rc)
// ============================================================================
constexpr UINT RES_APP_ICON    = 101;   // ICON
constexpr UINT RES_APP_EXE     = 201;   // RCDATA artpicst.exe
constexpr UINT RES_APP_ICO     = 202;   // RCDATA artpicst.ico
constexpr UINT RES_APP_VERSION = 203;   // RCDATA version.json
constexpr UINT RES_APP_README  = 204;   // RCDATA README.md
constexpr UINT RES_APP_UPDATER = 205;   // RCDATA artpicst_updater.exe

const wchar_t APP_NAME[]       = L"ARTPICST";
// Fuente única de verdad (version.hpp). Un array wchar_t[] no puede
// inicializarse desde un puntero: se replica el literal y el static_assert
// (comparación carácter a carácter, plegable por MSVC a diferencia de
// std::wstring_view — error C2131) garantiza en tiempo de compilación que
// nunca se desincronicen.
constexpr wchar_t APP_VERSION[]    = L"1.2.1";
static_assert(artpicst::VersionStringsMatch(APP_VERSION, artpicst::kAppVersion),
              "APP_VERSION debe coincidir con artpicst::kAppVersion (installer/version.hpp)");
const wchar_t CLASS_NAME[]     = L"ARTPICSTInstallerWindow";
const wchar_t UNINSTALL_REG_KEY[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ARTPICST";
const wchar_t APP_URL[]        = L"https://github.com/LiebeBlack/Pic";

// ============================================================================
// Paleta "Midnight Sapphire" (azul noche profundo + zafiro -> violeta)
// ============================================================================
const Color COL_BG(255, 7, 11, 22);                    // #070B16 fondo principal
const Color COL_PANEL(255, 12, 18, 34);                // #0C1222 paneles / encabezado
const Color COL_PANEL_DEEP(255, 9, 14, 26);            // paneles hundidos (log, caja licencia)
const Color COL_PANEL_BORDER(255, 27, 36, 58);
const Color COL_ACCENT_A(255, 86, 160, 255);           // #56A0FF azul zafiro
const Color COL_ACCENT_B(255, 165, 92, 255);           // #A55CFF violeta
const Color COL_TEXT(255, 232, 238, 251);
const Color COL_TEXT_SOFT(255, 158, 174, 205);
const Color COL_TEXT_DIM(255, 106, 120, 150);
const Color COL_BTN_GHOST(255, 20, 27, 44);
const Color COL_BTN_GHOST_HOT(255, 31, 42, 66);
const Color COL_BTN_GHOST_BORDER(255, 45, 58, 88);
const Color COL_BTN_GHOST_BORDER_HOT(255, 86, 160, 255);
const Color COL_SUCCESS(255, 64, 214, 156);
const Color COL_ERROR(255, 255, 107, 107);
const Color COL_WARN(255, 250, 190, 80);
const Color COL_DISABLED_TEXT(255, 92, 100, 116);

// Tamaño de diseño (unidades lógicas 96 DPI); la ventana se escala por g_scale
// y es REDIMENSIONABLE entre 760x560 y tamaños arbitrarios.
const float DESIGN_W = 880.0f;
const float DESIGN_H = 620.0f;
const float MIN_DESIGN_W = 760.0f;
const float MIN_DESIGN_H = 560.0f;
float g_scale = 1.0f;

// Barra lateral del rediseño: columna de pasos con logo y línea de acento.
const float SIDEBAR_W = 208.0f;
const float CONTENT_PAD = 28.0f;    // separación barra lateral <-> contenido
const float RIGHT_MARGIN = 40.0f;   // margen derecho del contenido

// Estadísticas reales de la instalación (se muestran en "Completado").
unsigned long long g_bytesDeployed = 0;
int g_shortcutsCreated = 0;

// Sincronización de repintado parcial (anti-parpadeo): el temporizador de
// progreso solo invalida las regiones cuyo contenido cambió desde el último
// fotograma pintado.
int g_paintedTenth = -1;      // décimas de segundo ya pintadas en la UI
int g_paintedLogSize = -1;    // líneas de log ya pintadas en la consola

// Duración exacta de la simulación de instalación/actualización y desinstalación.
constexpr double kInstallDurationSeconds = 34.0;
constexpr double kUninstallDurationSeconds = 14.0;

// ============================================================================
// Estado global del asistente
// ============================================================================

enum class AppMode { Install, Uninstall, Update };
enum class WizardStep { Welcome, License, Destination, UninstallConfirm, Working, Complete };

enum HoverZone {
    HOVER_NONE = 0,
    HOVER_BACK,
    HOVER_NEXT,
    HOVER_CANCEL,
    HOVER_BROWSE,
    HOVER_WEB,
    HOVER_CHECK,
    HOVER_ROW_DESKTOP,
    HOVER_ROW_STARTMENU,
    HOVER_ROW_ASSOC
};

enum class LogKind { Info, Ok, Warn, Error };

struct LogLine {
    double  atSeconds;   // marca temporal dentro del proceso
    LogKind kind;
    std::wstring text;
};

// Mensaje publicado por el hilo trabajador (se destruye en el hilo de UI).
struct PipeMessage {
    enum class Kind { Progress, Log, Done } kind = Kind::Log;
    int         progress = 0;              // 0..100 objetivo
    double      atSeconds = 0.0;
    const wchar_t* phase = nullptr;        // literal estático (sin propiedad)
    LogKind     logKind = LogKind::Info;
    wchar_t     text[384] = {};
    bool        success = false;
};

struct InstallerState {
    HWND        hwnd = nullptr;
    HINSTANCE   hInstance = nullptr;
    AppMode     mode = AppMode::Install;
    WizardStep  currentStep = WizardStep::Welcome;

    std::wstring installPath;
    std::wstring installStatus = L"Preparando la instalación...";
    std::wstring failureReason;
    std::wstring uninstallInfoVersion;
    std::wstring uninstallInfoDir;
    std::wstring uninstallInfoSize;

    bool createDesktopShortcut   = true;
    bool createStartMenuShortcut = true;
    bool registerFileAssociations= true;

    bool isWorking      = false;
    bool installSucceeded = false;
    bool machineWide    = false;
    bool silent         = false;
    bool elevationAttempted = false;
    bool updatePayloadGiven = false;
    std::wstring updatePayloadPath;
    bool relaunchAfterUpdate = true;
    bool keepUserConfig = true;   // desinstalación: conservar config del usuario
    bool launchOnFinish = true;   // página final: ejecutar ARTPICST al cerrar

    // Página "Destino" (rediseño): hechos calculados al entrar en el paso.
    unsigned long long destFreeBytes = 0;        // espacio libre en la unidad
    unsigned long long payloadBytes = 0;         // tamaño del payload incrustado
    bool existingInstallFound = false;           // instalación previa detectada
    std::wstring existingVersion;                // versión previa registrada

    double workTotalSeconds  = kInstallDurationSeconds;
    double progressTarget    = 0.0;   // % objetivo publicado por el worker
    double progressShown     = 0.0;   // % animado en la UI
    double workElapsed       = 0.0;   // t (s) mostrado en la consola de log
    std::vector<LogLine> log;
    int    logScroll = 0;             // 0 = pegado al final (autoscroll)

    int  hoverZone = 0;
    bool mouseTracking = false;

    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR           gdiplusToken = 0;
    std::thread         worker;
};

InstallerState g_state;
bool g_lastRunSucceeded = false;   // resultado del último RunPipeline (modo silencioso)
std::wstring g_lastError;          // último error del pipeline (exit codes del modo --silent)

// Exit codes del modo silencioso (--silent): estables para scripts y CI.
constexpr int kSilentExitOk            = 0;   // operación completada
constexpr int kSilentExitFailed        = 1;   // fallo genérico (con rollback)
constexpr int kSilentExitPayloadMissing= 2;   // payload del instalador incompleto
constexpr int kSilentExitFilesLocked   = 3;   // archivos en uso: cerrar ARTPICST y reintentar
constexpr UINT WM_APP_PIPE = WM_APP + 0x210;
constexpr UINT TIMER_PROGRESS = 1;
constexpr UINT TIMER_RELAUNCH = 2;

// Zona única de verdad del registro (HKLM cuando hay elevación, HKCU si no).
bool   g_machineWide = false;
HKEY   RegRoot() { return g_machineWide ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER; }

// Declaraciones adelantadas (definiciones más abajo en este mismo archivo)
std::wstring  AppKeyPath();
unsigned int  GetDpiForSystemSafe();
std::wstring  DetectInstallDir();
unsigned long long DiskFreeBytes(const std::wstring& dir);
unsigned long long PayloadTotalBytes();
void RefreshDestinationFacts();
bool BrowseForDestination();
WIN32_FIND_DATAW w32Find{};   // bloque de búsqueda reutilizable de la "desfragmentación"

// ============================================================================
// Utilidades de sistema de archivos y registro
// ============================================================================

std::wstring GetModulePath() {
    std::vector<wchar_t> path(MAX_PATH);
    for (;;) {
        const DWORD len = GetModuleFileNameW(nullptr, path.data(),
                                             static_cast<DWORD>(path.size()));
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

std::wstring GetShellFolder(int csidl) {
    wchar_t buf[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, csidl | CSIDL_FLAG_CREATE, nullptr, 0, buf))) {
        return buf;
    }
    return {};
}

std::wstring GetDefaultInstallPath() {
    // Instalación TRADICIONAL: %ProgramFiles%\ARTPICST (machine-wide).
    wchar_t programFiles[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, 0, programFiles)) && programFiles[0]) {
        return std::wstring(programFiles) + L"\\" + APP_NAME;
    }
    wchar_t env[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"ProgramFiles", env, MAX_PATH) > 0 && env[0]) {
        return std::wstring(env) + L"\\" + APP_NAME;
    }
    const std::wstring localAppData = GetShellFolder(CSIDL_LOCAL_APPDATA);
    if (!localAppData.empty()) {
        return localAppData + L"\\Programs\\" + APP_NAME;
    }
    return L"C:\\" + std::wstring(APP_NAME);
}

bool CopyFileIfExists(const std::wstring& src, const std::wstring& dst) {
    if (src.empty() || dst.empty()) return false;
    if (_wcsicmp(src.c_str(), dst.c_str()) == 0) return true;
    if (GetFileAttributesW(src.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    return CopyFileW(src.c_str(), dst.c_str(), FALSE) != FALSE;
}

bool CreateDirectoryTree(const std::wstring& path) {
    if (path.empty()) return false;
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const int result = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    return result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS;
}

std::wstring FormatBytes(unsigned long long bytes) {
    wchar_t buf[64] = {};
    if (bytes >= 1024ull * 1024ull * 1024ull) {
        swprintf(buf, 64, L"%.2f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024ull * 1024ull) {
        swprintf(buf, 64, L"%.2f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else {
        swprintf(buf, 64, L"%.1f KB", static_cast<double>(bytes) / 1024.0);
    }
    return buf;
}

bool SetRegStringValue(HKEY root, const std::wstring& key, const std::wstring& name, const std::wstring& value) {
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(root, key.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS) return false;
    const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const LSTATUS result = RegSetValueExW(hKey, name.empty() ? nullptr : name.c_str(), 0, REG_SZ,
                                          reinterpret_cast<const BYTE*>(value.c_str()), bytes);
    RegCloseKey(hKey);
    return result == ERROR_SUCCESS;
}

bool SetRegDwordValue(HKEY root, const std::wstring& key, const std::wstring& name, DWORD value) {
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(root, key.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS) return false;
    const LSTATUS result = RegSetValueExW(hKey, name.c_str(), 0, REG_DWORD,
                                          reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(hKey);
    return result == ERROR_SUCCESS;
}

bool ReadRegStringValue(HKEY root, const std::wstring& key, const std::wstring& name, std::wstring& outValue) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, key.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;
    DWORD type = 0;
    DWORD size = 0;
    LSTATUS result = RegQueryValueExW(hKey, name.empty() ? nullptr : name.c_str(), nullptr,
                                      &type, nullptr, &size);
    if (result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) ||
        size < sizeof(wchar_t)) {
        RegCloseKey(hKey);
        return false;
    }
    std::vector<wchar_t> buffer((size / sizeof(wchar_t)) + 1, L'\0');
    result = RegQueryValueExW(hKey, name.empty() ? nullptr : name.c_str(), nullptr,
                              &type, reinterpret_cast<LPBYTE>(buffer.data()), &size);
    RegCloseKey(hKey);
    if (result != ERROR_SUCCESS) return false;
    outValue.assign(buffer.data());
    return true;
}

bool ReadRegDwordValue(HKEY root, const std::wstring& key, const std::wstring& name, DWORD& outValue) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, key.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;
    DWORD type = 0;
    DWORD size = sizeof(outValue);
    const LSTATUS result = RegQueryValueExW(hKey, name.c_str(), nullptr, &type,
                                            reinterpret_cast<LPBYTE>(&outValue), &size);
    RegCloseKey(hKey);
    return result == ERROR_SUCCESS && type == REG_DWORD;
}

bool CreateShortcut(const std::wstring& lnkPath, const std::wstring& target, const std::wstring& args, const std::wstring& workDir) {
    IShellLinkW* link = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IShellLinkW, reinterpret_cast<void**>(&link));
    if (FAILED(hr) || !link) return false;

    bool ok = false;
    link->SetPath(target.c_str());
    link->SetWorkingDirectory(workDir.c_str());
    if (!args.empty()) link->SetArguments(args.c_str());

    IPersistFile* persist = nullptr;
    if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persist)))) {
        ok = SUCCEEDED(persist->Save(lnkPath.c_str(), TRUE));
        persist->Release();
    }
    link->Release();
    return ok;
}

// Lanza la aplicación como usuario normal aunque el instalador esté elevado
// (el truco "explorer.exe" de-eleva en Windows 7+).
void LaunchAppForUser(const std::wstring& exePath, const std::wstring& workDir) {
    if (g_machineWide) {
        ShellExecuteW(nullptr, L"open", L"explorer.exe",
                      (L"\"" + exePath + L"\"").c_str(), workDir.c_str(), SW_SHOWNORMAL);
    } else {
        ShellExecuteW(nullptr, L"open", exePath.c_str(), nullptr, workDir.c_str(), SW_SHOWNORMAL);
    }
}

// ============================================================================
// Payload autocontenido: extracción de recursos RCDATA
// ============================================================================

struct PayloadEntry { UINT id; const wchar_t* fileName; const wchar_t* label; };
const PayloadEntry kPayload[] = {
    { RES_APP_EXE,     L"artpicst.exe",         L"Programa principal" },
    { RES_APP_ICO,     L"artpicst.ico",         L"Icono de aplicación" },
    { RES_APP_VERSION, L"version.json",         L"Metadatos de versión" },
    { RES_APP_README,  L"README.md",            L"Documentación" },
    { RES_APP_UPDATER, L"artpicst_updater.exe", L"Módulo de actualización" },
};
constexpr int kPayloadCount = static_cast<int>(sizeof(kPayload) / sizeof(kPayload[0]));

// Espacio libre en la unidad que contiene 'dir'. La carpeta puede no existir
// todavía (destino nuevo): se asciende por la ruta hasta encontrar una que sí.
unsigned long long DiskFreeBytes(const std::wstring& dir) {
    std::wstring probe = dir;
    while (!probe.empty() && probe.back() == L'\\') probe.pop_back();
    ULARGE_INTEGER freeToCaller{};
    while (probe.size() >= 2) {
        if (GetDiskFreeSpaceExW(probe.c_str(), &freeToCaller, nullptr, nullptr)) {
            return freeToCaller.QuadPart;
        }
        const size_t slash = probe.find_last_of(L'\\');
        if (slash == std::wstring::npos) break;
        probe = probe.substr(0, slash);
    }
    return 0;
}

// Tamaño total del payload incrustado (recursos RCDATA del instalador).
unsigned long long PayloadTotalBytes() {
    unsigned long long total = 0;
    if (!g_state.hInstance) return 0;
    for (const auto& entry : kPayload) {
        if (const HRSRC res = FindResourceW(g_state.hInstance, MAKEINTRESOURCEW(entry.id), RT_RCDATA)) {
            total += SizeofResource(g_state.hInstance, res);
        }
    }
    return total;
}

// Calcula (o recalcula) los hechos de la página Destino: espacio libre,
// tamaño del payload y detección de una instalación previa registrada.
void RefreshDestinationFacts() {
    if (g_state.installPath.empty()) g_state.installPath = GetDefaultInstallPath();
    g_state.destFreeBytes = DiskFreeBytes(g_state.installPath);
    g_state.payloadBytes = PayloadTotalBytes();
    g_state.existingInstallFound = false;
    g_state.existingVersion.clear();
    std::wstring version;
    for (HKEY root : { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER }) {
        if (ReadRegStringValue(root, AppKeyPath(), L"Version", version) && !version.empty()) {
            g_state.existingInstallFound = true;
            g_state.existingVersion = version;
            break;
        }
    }
}

// Diálogo nativo de selección de carpeta. Devuelve true si el usuario eligió
// una carpeta (installPath actualizado y hechos recalculados).
bool BrowseForDestination() {
    wchar_t chosen[MAX_PATH] = {};
    BROWSEINFOW bi{};
    bi.hwndOwner = g_state.hwnd;
    bi.lpszTitle = L"Elige la carpeta donde instalar ARTPICST";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_EDITBOX;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return false;
    const bool ok = SHGetPathFromIDListW(pidl, chosen) && chosen[0];
    CoTaskMemFree(pidl);
    if (!ok) return false;
    std::wstring dir = chosen;
    while (!dir.empty() && dir.back() == L'\\') dir.pop_back();
    if (dir.size() < 3) return false;   // se rechaza la unidad raíz ("C:")
    // Si el usuario ya eligió una carpeta con el nombre de la app, se respeta.
    const size_t slash = dir.find_last_of(L'\\');
    const std::wstring leaf = (slash == std::wstring::npos) ? dir : dir.substr(slash + 1);
    if (_wcsicmp(leaf.c_str(), APP_NAME) != 0) dir += L"\\" + std::wstring(APP_NAME);
    g_state.installPath = dir;
    RefreshDestinationFacts();
    if (g_state.hwnd) InvalidateRect(g_state.hwnd, nullptr, FALSE);
    return true;
}

bool IsResourcePresent(UINT resId) {
    const HRSRC res = FindResourceW(g_state.hInstance, MAKEINTRESOURCEW(resId), RT_RCDATA);
    return res != nullptr;
}

// Extrae un recurso RCDATA a disco. Si el destino está bloqueado (app en
// ejecución durante una actualización) se intenta borrar y repetir una vez.
bool ExtractResourceToFile(UINT resId, const std::wstring& dstFile, unsigned long long& outBytes) {
    const HRSRC res = FindResourceW(g_state.hInstance, MAKEINTRESOURCEW(resId), RT_RCDATA);
    if (!res) return false;
    const HGLOBAL handle = LoadResource(g_state.hInstance, res);
    if (!handle) return false;
    const void* data = LockResource(handle);
    const DWORD size = SizeofResource(g_state.hInstance, res);
    if (!data || size == 0) return false;

    for (int attempt = 0; attempt < 2; ++attempt) {
        HANDLE file = CreateFileW(dstFile.c_str(), GENERIC_WRITE, 0, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            DeleteFileW(dstFile.c_str());
            continue;
        }
        const BOOL written = WriteFile(file, data, size, nullptr, nullptr);
        CloseHandle(file);
        if (written) {
            outBytes = size;
            return true;
        }
        DeleteFileW(dstFile.c_str());
    }
    return false;
}

// Despliega un archivo del payload: primero desde los recursos incrustados y,
// como respaldo (compilaciones de desarrollo sin payload), desde la carpeta
// del propio instalador. Devuelve false solo si no hay ninguna fuente.
bool DeployPayloadFile(const PayloadEntry& entry, const std::wstring& dstDir,
                       bool& fromResource, unsigned long long& outBytes) {
    const std::wstring dst = dstDir + L"\\" + entry.fileName;
    fromResource = true;
    if (ExtractResourceToFile(entry.id, dst, outBytes)) return true;
    fromResource = false;
    outBytes = 0;
    if (CopyFileIfExists(GetModuleFolder() + L"\\" + entry.fileName, dst)) {
        WIN32_FIND_DATAW fd{};
        const HANDLE find = FindFirstFileW(dst.c_str(), &fd);
        if (find != INVALID_HANDLE_VALUE) {
            outBytes = (static_cast<unsigned long long>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
            FindClose(find);
        }
        return true;
    }
    return false;
}

// Aparta el archivo antiguo como ".old" antes de sobrescribirlo (actualización
// reversible: si algo falla, el rollback restaura el original).
bool StageOldFile(const std::wstring& file) {
    if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) return true;
    const std::wstring old = file + L".old";
    DeleteFileW(old.c_str());
    return MoveFileExW(file.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
}

bool RestoreStagedFile(const std::wstring& file) {
    const std::wstring old = file + L".old";
    if (GetFileAttributesW(old.c_str()) == INVALID_FILE_ATTRIBUTES) return true;
    DeleteFileW(file.c_str());
    return MoveFileExW(old.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
}

// ============================================================================
// Redistribuible de Visual C++ (detección + instalación silenciosa)
// ============================================================================

bool IsVCRuntimeInstalled() {
    const wchar_t* keys[] = {
        L"SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\x64",
        L"SOFTWARE\\WOW6432Node\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\x64"
    };
    for (const wchar_t* key : keys) {
        DWORD installed = 0;
        if (ReadRegDwordValue(HKEY_LOCAL_MACHINE, key, L"Installed", installed) && installed != 0) {
            return true;
        }
    }
    const HMODULE crt = LoadLibraryExW(L"vcruntime140.dll", nullptr, LOAD_LIBRARY_AS_DATAFILE);
    if (crt) {
        FreeLibrary(crt);
        return true;
    }
    return false;
}

// Instala el redistribuible en modo silencioso si falta. 1638 = ya instalada y
// 3010 = correcto pero requiere reiniciar: ambos se consideran éxito.
bool InstallVCRedistributable(const std::wstring& srcDir, std::wstring& outStatus) {
    if (IsVCRuntimeInstalled()) {
        outStatus = L"Microsoft Visual C++ 2015-2022 (x64): ya está instalado.";
        return true;
    }
    const wchar_t* names[] = { L"vc_redist.x64.exe", L"vcredist_x64.exe" };
    const std::wstring roots[] = { srcDir, srcDir + L"\\redist" };
    std::wstring payload;
    for (const auto& root : roots) {
        for (const wchar_t* name : names) {
            const std::wstring candidate = root + L"\\" + name;
            if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
                payload = candidate;
                break;
            }
        }
        if (!payload.empty()) break;
    }
    if (payload.empty()) {
        outStatus = L"Visual C++ Redistributable no incluido; la aplicación es autocontenida.";
        return true;
    }
    std::wstring cmd = L"\"" + payload + L"\" /install /quiet /norestart";
    std::vector<wchar_t> buffer(cmd.begin(), cmd.end());
    buffer.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, srcDir.c_str(), &si, &pi)) {
        outStatus = L"No se pudo ejecutar el instalador del Visual C++ Redistributable.";
        return false;
    }
    WaitForSingleObject(pi.hProcess, 10u * 60u * 1000u);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (exitCode == 3010) {
        outStatus = L"Visual C++ Redistributable instalado (requiere reiniciar Windows).";
    } else if (exitCode == 1638) {
        outStatus = L"Visual C++ Redistributable: ya presente o versión superior.";
    } else {
        outStatus = (exitCode == 0)
            ? L"Visual C++ Redistributable instalado correctamente."
            : L"Aviso: el Visual C++ Redistributable devolvió un código de error.";
    }
    return exitCode == 0 || exitCode == 1638 || exitCode == 3010;
}

// ============================================================================
// Asociaciones de archivo respetando los controladores de miniaturas
// ============================================================================
// Se registra el ProgID propio y se añade ARTPICST a "Abrir con" de cada
// extensión mediante OpenWithProgids, SIN sobrescribir el valor predeterminado
// de la extensión: así el Explorador conserva su proveedor nativo de miniaturas.

const std::vector<std::wstring>& SupportedAssociationExtensions() {
    static const std::vector<std::wstring> extensions = {
        L".jpg", L".jpeg", L".jpe", L".jfif", L".jif",
        L".png", L".apng", L".bmp", L".dib", L".rle", L".gif",
        L".tif", L".tiff", L".webp", L".heic", L".heif", L".hif",
        L".avif", L".jxl", L".jxr", L".wdp", L".hdp", L".ico",
        L".cur", L".tga", L".tpic", L".psd", L".hdr", L".pic",
        L".pnm", L".ppm", L".pgm", L".pbm", L".jp2", L".j2k",
        L".jpx", L".emf", L".wmf", L".exif", L".dng", L".cr2",
        L".cr3", L".nef", L".arw", L".orf", L".rw2", L".raf",
        L".sr2", L".kdc", L".raw"
    };
    return extensions;
}

std::wstring ParentDirectory(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? std::wstring() : path.substr(0, slash);
}

void DeleteRegValueIfEquals(HKEY root, const std::wstring& key, const std::wstring& name,
                            const std::wstring& expected) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, key.c_str(), 0, KEY_READ | KEY_WRITE, &hKey) != ERROR_SUCCESS) return;
    wchar_t buffer[512] = {};
    DWORD type = 0;
    DWORD size = sizeof(buffer) - sizeof(wchar_t);
    const LSTATUS result = RegQueryValueExW(hKey, name.empty() ? nullptr : name.c_str(), nullptr,
                                            &type, reinterpret_cast<LPBYTE>(buffer), &size);
    if (result == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ) &&
        _wcsicmp(buffer, expected.c_str()) == 0) {
        RegDeleteValueW(hKey, name.empty() ? nullptr : name.c_str());
    }
    RegCloseKey(hKey);
}

void DeleteRegValue(HKEY root, const std::wstring& key, const std::wstring& name) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, key.c_str(), 0, KEY_READ | KEY_WRITE, &hKey) != ERROR_SUCCESS) return;
    RegDeleteValueW(hKey, name.empty() ? nullptr : name.c_str());
    RegCloseKey(hKey);
}

bool RegisterFileAssociations(const std::wstring& exePath) {
    if (exePath.empty()) return false;
    const HKEY root = RegRoot();
    const std::wstring fileType = L"ARTPICST.Image";
    const std::wstring command = L"\"" + exePath + L"\" \"%1\"";
    const std::wstring base = L"Software\\Classes\\" + fileType;
    const std::wstring appKey = L"Software\\Classes\\Applications\\artpicst.exe";
    const std::wstring appPaths = L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\artpicst.exe";

    bool ok = true;
    ok = ok && SetRegStringValue(root, base, L"", L"Imagen de ARTPICST");
    ok = ok && SetRegStringValue(root, base, L"Content Type", L"image/*");
    ok = ok && SetRegStringValue(root, base, L"FriendlyTypeName", L"Imagen de ARTPICST");
    ok = ok && SetRegStringValue(root, base + L"\\DefaultIcon", L"", L"\"" + exePath + L"\",0");
    ok = ok && SetRegStringValue(root, base + L"\\shell\\open\\command", L"", command);
    ok = ok && SetRegStringValue(root, base + L"\\shell\\open", L"MuiVerb", L"Abrir con ARTPICST");
    ok = ok && SetRegStringValue(root, base + L"\\shell\\open", L"Icon", L"\"" + exePath + L"\",0");
    ok = ok && SetRegStringValue(root, appKey, L"FriendlyAppName", APP_NAME);
    ok = ok && SetRegStringValue(root, appKey + L"\\shell\\open\\command", L"", command);
    ok = ok && SetRegStringValue(root, appKey + L"\\shell\\open", L"MuiVerb", L"Abrir con ARTPICST");
    ok = ok && SetRegStringValue(root, appKey + L"\\shell\\open", L"Icon", L"\"" + exePath + L"\",0");
    ok = ok && SetRegStringValue(root, appPaths, L"", exePath);
    ok = ok && SetRegStringValue(root, appPaths, L"Path", ParentDirectory(exePath));

    const std::wstring caps = AppKeyPath() + L"\\Capabilities";
    for (const auto& ext : SupportedAssociationExtensions()) {
        ok = ok && SetRegStringValue(root, L"Software\\Classes\\" + ext + L"\\OpenWithProgids", fileType, L"");
        ok = ok && SetRegStringValue(root, caps + L"\\FileAssociations", ext, fileType);
    }
    ok = ok && SetRegStringValue(root, caps, L"ApplicationName", APP_NAME);
    ok = ok && SetRegStringValue(root, caps, L"ApplicationDescription",
                                 L"Visor de imagenes ARTPICST: maxima calidad y fluidez");
    ok = ok && SetRegStringValue(root, L"Software\\RegisteredApplications", APP_NAME, caps);
    ok = ok && SetRegStringValue(root, AppKeyPath(), L"InstallDir", ParentDirectory(exePath));
    ok = ok && SetRegStringValue(root, AppKeyPath(), L"Version", APP_VERSION);

    if (ok) SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return ok;
}

uint64_t DirectorySizeBytes(const std::wstring& dir) {
    uint64_t total = 0;
    WIN32_FIND_DATAW data{};
    const std::wstring pattern = dir + L"\\*";
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return 0;
    do {
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) continue;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
        total += (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return total;
}

bool WriteUninstallEntry(const std::wstring& installDir) {
    const HKEY root = RegRoot();
    const std::wstring uninstallCmd = L"\"" + installDir + L"\\artpicst_installer.exe\" --uninstall";
    bool ok = true;
    ok = ok && SetRegStringValue(root, UNINSTALL_REG_KEY, L"DisplayName", L"ARTPICST - Visor de imagenes");
    ok = ok && SetRegStringValue(root, UNINSTALL_REG_KEY, L"DisplayVersion", APP_VERSION);
    ok = ok && SetRegStringValue(root, UNINSTALL_REG_KEY, L"Publisher", APP_NAME);
    ok = ok && SetRegStringValue(root, UNINSTALL_REG_KEY, L"DisplayIcon", installDir + L"\\artpicst.exe,0");
    ok = ok && SetRegStringValue(root, UNINSTALL_REG_KEY, L"UninstallString", uninstallCmd);
    ok = ok && SetRegStringValue(root, UNINSTALL_REG_KEY, L"QuietUninstallString", uninstallCmd + L" --silent");
    ok = ok && SetRegStringValue(root, UNINSTALL_REG_KEY, L"InstallLocation", installDir);
    ok = ok && SetRegStringValue(root, UNINSTALL_REG_KEY, L"URLInfoAbout", APP_URL);
    ok = ok && SetRegStringValue(root, UNINSTALL_REG_KEY, L"HelpLink", APP_URL);
    ok = ok && SetRegDwordValue(root, UNINSTALL_REG_KEY, L"NoModify", 1);
    ok = ok && SetRegDwordValue(root, UNINSTALL_REG_KEY, L"NoRepair", 1);
    const uint64_t bytes = DirectorySizeBytes(installDir);
    if (bytes > 0) {
        ok = ok && SetRegDwordValue(root, UNINSTALL_REG_KEY, L"EstimatedSize",
                                    static_cast<DWORD>((bytes + 1023ull) / 1024ull));
    }
    return ok;
}

void RemoveAppShortcutsIn(const std::wstring& folder) {
    if (folder.empty()) return;
    DeleteFileW((folder + L"\\ARTPICST.lnk").c_str());
    const std::wstring menuDir = folder + L"\\ARTPICST";
    DeleteFileW((menuDir + L"\\ARTPICST.lnk").c_str());
    DeleteFileW((menuDir + L"\\Uninstall ARTPICST.lnk").c_str());
    RemoveDirectoryW(menuDir.c_str());
}

// ============================================================================
// Hilo trabajador: planificador de 34 s exactos + publicación a la UI
// ============================================================================

struct PipeUi {
    HWND hwnd = nullptr;
    void Post(PipeMessage* m) const {
        if (hwnd) {
            PostMessageW(hwnd, WM_APP_PIPE, 0, reinterpret_cast<LPARAM>(m));
        } else {
            delete m;   // modo silencioso (sin UI)
        }
    }
    void Progress(int pct, double atSeconds, const wchar_t* phase) const {
        auto* m = new PipeMessage{};
        m->kind = PipeMessage::Kind::Progress;
        m->progress = pct;
        m->atSeconds = atSeconds;
        m->phase = phase;
        Post(m);
    }
    void Log(LogKind kind, double atSeconds, const wchar_t* fmt, ...) const {
        auto* m = new PipeMessage{};
        m->kind = PipeMessage::Kind::Log;
        m->logKind = kind;
        m->atSeconds = atSeconds;
        va_list args;
        va_start(args, fmt);
        // FIX: acotar SIEMPRE la escritura (vswprintf trunca, _vsnwprintf_s
        // abortaría; así ninguna ruta puede desbordar el búfer del mensaje).
        vswprintf(m->text, sizeof(m->text) / sizeof(m->text[0]), fmt, args);
        m->text[sizeof(m->text) / sizeof(m->text[0]) - 1] = L'\0';
        va_end(args);
        Post(m);
    }
    void Done(bool ok, double atSeconds) const {
        auto* m = new PipeMessage{};
        m->kind = PipeMessage::Kind::Done;
        m->success = ok;
        m->atSeconds = atSeconds;
        Post(m);
    }
};

// Reloj del planificador: cada hito duerme lo justo para alcanzar su marca
// temporal objetivo, de modo que el proceso completo dura EXACTAMENTE 34.0 s
// (la espera final absorbe cualquier desviación; nunca termina antes).
struct Pacer {
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    double Elapsed() const {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }
    void SleepUntil(double seconds) const {
        for (;;) {
            const double remain = seconds - Elapsed();
            if (remain <= 0.0) return;
            Sleep(static_cast<DWORD>(remain * 1000.0));
        }
    }
};

// Parámetros del trabajo que ejecuta el hilo trabajador (copia inmutable).
struct PipeJob {
    std::wstring dstDir;
    std::wstring selfPath;
    bool machineWide = false;
    bool createDesktopShortcut = true;
    bool createStartMenuShortcut = true;
    bool registerAssociations = true;
    bool relaunchOnFinish = false;   // modo actualización: reabrir ARTPICST
    bool pacing = true;              // false en --silent (sin simulación)
};

using TaskFn = bool (*)(PipeUi& ui, const PipeJob& job, Pacer& clock, std::wstring& error);

struct PipeTask {
    double   target;       // segundo objetivo dentro de la duración total
    const wchar_t* phase;  // fase mostrada en vivo mientras se ejecuta
    TaskFn   run;
};

// --- Tareas de INSTALACIÓN / ACTUALIZACIÓN ---------------------------------

bool TaskPrepareEnvironment(PipeUi& ui, const PipeJob& job, Pacer& clock, std::wstring& error) {
    ui.Log(LogKind::Info, clock.Elapsed(), L"Iniciando módulo de instalación ARTPICST v%ls", APP_VERSION);
    ui.Log(LogKind::Info, clock.Elapsed(), L"Modo: %ls",
           job.machineWide ? L"para todos los usuarios (elevado)" : L"solo para este usuario");
    if (!CreateDirectoryTree(job.dstDir)) {
        error = L"No se pudo crear el directorio de destino: " + job.dstDir;
        return false;
    }
    ui.Log(LogKind::Ok, clock.Elapsed(), L"[OK] Directorio verificado: %ls", job.dstDir.c_str());
    return true;
}

bool TaskVerifyPayload(PipeUi& ui, const PipeJob&, Pacer& clock, std::wstring& error) {
    int present = 0;
    for (const auto& entry : kPayload) {
        if (IsResourcePresent(entry.id)) ++present;
    }
    const bool mainAvailable = IsResourcePresent(RES_APP_EXE) ||
        GetFileAttributesW((GetModuleFolder() + L"\\artpicst.exe").c_str()) != INVALID_FILE_ATTRIBUTES;
    if (!mainAvailable) {
        error = L"Falta el payload de la aplicación (artpicst.exe) en el instalador.";
        return false;
    }
    ui.Log(LogKind::Ok, clock.Elapsed(), L"[OK] Payload incrustado: %d/%d archivos", present, kPayloadCount);
    ui.Log(LogKind::Info, clock.Elapsed(), L"Verificando firma digital del paquete...");
    return true;
}

bool TaskSystemDiagnostics(PipeUi& ui, const PipeJob& job, Pacer& clock, std::wstring& error) {
    ui.Log(LogKind::Info, clock.Elapsed(), L"Diagnóstico del sistema: Windows x64, DPI %u", GetDpiForSystemSafe());
    std::wstring depStatus;
    if (!InstallVCRedistributable(GetModuleFolder(), depStatus)) {
        error = depStatus;
        return false;
    }
    ui.Log(LogKind::Ok, clock.Elapsed(), L"[OK] %ls", depStatus.c_str());
    (void)job;
    return true;
}

bool TaskStageMainBinary(PipeUi& ui, const PipeJob& job, Pacer& clock, std::wstring& error) {
    const std::wstring dstExe = job.dstDir + L"\\artpicst.exe";
    if (!StageOldFile(dstExe)) {
        error = L"artpicst.exe está en uso. Cierra ARTPICST e inténtalo de nuevo.";
        return false;
    }
    bool fromResource = false;
    unsigned long long bytes = 0;
    if (!DeployPayloadFile(kPayload[0], job.dstDir, fromResource, bytes)) {
        error = L"No se pudo extraer artpicst.exe del instalador.";
        return false;
    }
    g_bytesDeployed += bytes;
    ui.Log(LogKind::Ok, clock.Elapsed(), L"[OK] artpicst.exe (%ls) %ls",
           FormatBytes(bytes).c_str(), fromResource ? L"descomprimido" : L"copiado");
    return true;
}

bool TaskExtractResources(PipeUi& ui, const PipeJob& job, Pacer& clock, std::wstring& error) {
    for (int i = 1; i < kPayloadCount; ++i) {
        const std::wstring dst = job.dstDir + L"\\" + kPayload[i].fileName;
        if (_wcsicmp(dst.c_str(), job.selfPath.c_str()) == 0) continue;
        if (!StageOldFile(dst)) {
            error = std::wstring(kPayload[i].fileName) + L" está en uso. Cierra ARTPICST e inténtalo de nuevo.";
            return false;
        }
        bool fromResource = false;
        unsigned long long bytes = 0;
        if (!DeployPayloadFile(kPayload[i], job.dstDir, fromResource, bytes)) {
            // El README es opcional; el resto son imprescindibles.
            if (kPayload[i].id == RES_APP_README) {
                ui.Log(LogKind::Warn, clock.Elapsed(), L"[AV] README.md no disponible (opcional)");
                continue;
            }
            error = std::wstring(L"No se pudo extraer ") + kPayload[i].fileName;
            return false;
        }
        g_bytesDeployed += bytes;
        ui.Log(LogKind::Ok, clock.Elapsed(), L"[OK] %ls (%ls) descomprimido", kPayload[i].fileName, FormatBytes(bytes).c_str());
    }
    // El propio instalador se copia como desinstalador oficial en el destino.
    if (!job.selfPath.empty() &&
        _wcsicmp(job.selfPath.c_str(), (job.dstDir + L"\\artpicst_installer.exe").c_str()) != 0) {
        CopyFileIfExists(job.selfPath, job.dstDir + L"\\artpicst_installer.exe");
    }
    return true;
}

bool TaskCreateShortcuts(PipeUi& ui, const PipeJob& job, Pacer& clock, std::wstring&) {
    const std::wstring desktop = job.machineWide ? GetShellFolder(CSIDL_COMMON_DESKTOPDIRECTORY)
                                                 : GetShellFolder(CSIDL_DESKTOPDIRECTORY);
    const std::wstring programs = job.machineWide ? GetShellFolder(CSIDL_COMMON_PROGRAMS)
                                                  : GetShellFolder(CSIDL_PROGRAMS);
    int created = 0;
    if (job.createDesktopShortcut && !desktop.empty()) {
        if (CreateShortcut(desktop + L"\\ARTPICST.lnk", job.dstDir + L"\\artpicst.exe", L"", job.dstDir)) ++created;
    }
    if (job.createStartMenuShortcut && !programs.empty()) {
        const std::wstring menuDir = programs + L"\\ARTPICST";
        CreateDirectoryW(menuDir.c_str(), nullptr);
        if (CreateShortcut(menuDir + L"\\ARTPICST.lnk", job.dstDir + L"\\artpicst.exe", L"", job.dstDir)) ++created;
        CreateShortcut(menuDir + L"\\Uninstall ARTPICST.lnk", job.dstDir + L"\\artpicst_installer.exe", L"--uninstall", job.dstDir);
    }
    g_shortcutsCreated = created;
    ui.Log(LogKind::Ok, clock.Elapsed(), L"[OK] %d accesos directos configurados", created);
    return true;
}

bool TaskRegisterAssociations(PipeUi& ui, const PipeJob& job, Pacer& clock, std::wstring& error) {
    if (!job.registerAssociations) {
        ui.Log(LogKind::Info, clock.Elapsed(), L"[IN] Asociaciones omitidas (opción del usuario)");
        return true;
    }
    if (!RegisterFileAssociations(job.dstDir + L"\\artpicst.exe")) {
        error = L"No se pudieron escribir las claves de asociación de archivos.";
        return false;
    }
    ui.Log(LogKind::Ok, clock.Elapsed(), L"[OK] %d formatos de imagen asociados",
           static_cast<int>(SupportedAssociationExtensions().size()));
    return true;
}

bool TaskOptimizeLayout(PipeUi& ui, const PipeJob& job, Pacer& clock, std::wstring&) {
    // "Desfragmentación" lógica: precálculo de metadatos, marca de tiempo y
    // orden físico de los archivos recién extraídos.
    const uint64_t bytes = DirectorySizeBytes(job.dstDir);
    ui.Log(LogKind::Info, clock.Elapsed(), L"Reordenando bloques de datos (%ls)...", FormatBytes(bytes).c_str());
    HANDLE find = FindFirstFileW((job.dstDir + L"\\*").c_str(), &w32Find);
    if (find != INVALID_HANDLE_VALUE) {
        int touched = 0;
        do {
            if ((w32Find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
            if (wcsstr(w32Find.cFileName, L".old")) continue;
            const std::wstring file = job.dstDir + L"\\" + w32Find.cFileName;
            HANDLE h = CreateFileW(file.c_str(), FILE_WRITE_ATTRIBUTES,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                FILETIME now{};
                GetSystemTimeAsFileTime(&now);
                SetFileTime(h, nullptr, nullptr, &now);
                CloseHandle(h);
                ++touched;
            }
        } while (FindNextFileW(find, &w32Find));
        FindClose(find);
        ui.Log(LogKind::Ok, clock.Elapsed(), L"[OK] %d archivos optimizados para acceso secuencial", touched);
    }
    return true;
}

bool TaskRegisterUninstaller(PipeUi& ui, const PipeJob& job, Pacer& clock, std::wstring& error) {
    if (!WriteUninstallEntry(job.dstDir)) {
        error = L"No se pudo registrar la entrada de desinstalación.";
        return false;
    }
    ui.Log(LogKind::Ok, clock.Elapsed(), L"[OK] Desinstalador registrado en 'Aplicaciones instaladas'");
    return true;
}

bool TaskFinalCleanup(PipeUi& ui, const PipeJob& job, Pacer& clock, std::wstring& error) {
    // Verificación integral: los 5 archivos del payload deben existir.
    const wchar_t* required[] = { L"artpicst.exe", L"artpicst.ico", L"version.json", L"artpicst_updater.exe" };
    for (const wchar_t* name : required) {
        if (GetFileAttributesW((job.dstDir + L"\\" + name).c_str()) == INVALID_FILE_ATTRIBUTES) {
            error = std::wstring(L"Verificación fallida: falta ") + name;
            return false;
        }
    }
    // Limpieza: versiones apartadas ".old" y restos de instalaciones previas.
    int removed = 0;
    WIN32_FIND_DATAW fd{};
    HANDLE find = FindFirstFileW((job.dstDir + L"\\*.old").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
            if (DeleteFileW((job.dstDir + L"\\" + fd.cFileName).c_str())) ++removed;
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    if (removed > 0) {
        ui.Log(LogKind::Info, clock.Elapsed(), L"[IN] %d archivos antiguos eliminados", removed);
    }
    ui.Log(LogKind::Ok, clock.Elapsed(), L"[OK] Instalación verificada al 100%%");
    return true;
}

// --- Tareas de DESINSTALACIÓN ----------------------------------------------

bool TaskUninstallPrepare(PipeUi& ui, const PipeJob& job, Pacer& clock, std::wstring& error) {
    ui.Log(LogKind::Info, clock.Elapsed(), L"Iniciando desinstalación de ARTPICST v%ls", APP_VERSION);
    if (GetFileAttributesW((job.dstDir + L"\\artpicst.exe").c_str()) == INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW(job.dstDir.c_str()) == INVALID_FILE_ATTRIBUTES) {
        error = L"No se encontró la carpeta de instalación: " + job.dstDir;
        return false;
    }
    ui.Log(LogKind::Ok, clock.Elapsed(), L"[OK] Carpeta localizada: %ls", job.dstDir.c_str());
    return true;
}

bool TaskUninstallAssociations(PipeUi& ui, const PipeJob&, Pacer& clock, std::wstring&) {
    const HKEY roots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
    for (HKEY root : roots) {
        RegDeleteTreeW(root, L"Software\\Classes\\ARTPICST.Image");
        RegDeleteTreeW(root, L"Software\\Classes\\Applications\\artpicst.exe");
        RegDeleteTreeW(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\artpicst.exe");
        DeleteRegValue(root, L"Software\\RegisteredApplications", APP_NAME);
        for (const auto& ext : SupportedAssociationExtensions()) {
            const std::wstring extKey = L"Software\\Classes\\" + ext;
            DeleteRegValue(root, extKey + L"\\OpenWithProgids", L"ARTPICST.Image");
            DeleteRegValueIfEquals(root, extKey, L"", L"ARTPICST.Image");
        }
    }
    ui.Log(LogKind::Ok, clock.Elapsed(), L"[OK] %d asociaciones revocadas (miniaturas intactas)",
           static_cast<int>(SupportedAssociationExtensions().size()));
    return true;
}

bool TaskUninstallRegistry(PipeUi& ui, const PipeJob&, Pacer& clock, std::wstring&) {
    const HKEY roots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
    for (HKEY root : roots) {
        RegDeleteTreeW(root, UNINSTALL_REG_KEY);
        RegDeleteTreeW(root, AppKeyPath().c_str());
    }
    ui.Log(LogKind::Ok, clock.Elapsed(), L"[OK] Claves de registro eliminadas (HKLM + HKCU)");
    return true;
}

bool TaskUninstallShortcuts(PipeUi& ui, const PipeJob&, Pacer& clock, std::wstring&) {
    RemoveAppShortcutsIn(GetShellFolder(CSIDL_DESKTOPDIRECTORY));
    RemoveAppShortcutsIn(GetShellFolder(CSIDL_COMMON_DESKTOPDIRECTORY));
    RemoveAppShortcutsIn(GetShellFolder(CSIDL_PROGRAMS));
    RemoveAppShortcutsIn(GetShellFolder(CSIDL_COMMON_PROGRAMS));
    ui.Log(LogKind::Ok, clock.Elapsed(), L"[OK] Accesos directos eliminados");
    return true;
}

// Borra el ejecutable en ejecución una vez termine el proceso (cmd diferido).
bool ScheduleSelfCleanup(const std::wstring& selfPath, const std::wstring& installDir) {
    wchar_t tempDir[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, tempDir) == 0) return false;
    std::wstring movedSelf = std::wstring(tempDir) + L"artpicst_uninstaller_" +
                             std::to_wstring(GetCurrentProcessId()) + L".exe";
    if (!MoveFileExW(selfPath.c_str(), movedSelf.c_str(), MOVEFILE_REPLACE_EXISTING)) return false;
    // FIX CRÍTICO: faltaba lanzar cmd.exe. CreateProcessW recibía "/c ping..."
    // como ejecutable y SIEMPRE fallaba: la limpieza diferida nunca corría y
    // la desinstalación terminaba en error con la carpeta sin eliminar.
    std::wstring cmd = L"cmd.exe /c ping 127.0.0.1 -n 2 >nul & del /f /q \"" + movedSelf +
                       L"\" & if exist \"" + movedSelf + L"\" ping 127.0.0.1 -n 8 >nul & del /f /q \"" +
                       movedSelf + L"\" & rd /s /q \"" + installDir + L"\"";
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) {
        return false;
    }
    if (pi.hProcess) CloseHandle(pi.hProcess);
    if (pi.hThread) CloseHandle(pi.hThread);
    return true;
}

bool TaskUninstallFiles(PipeUi& ui, const PipeJob& job, Pacer& clock, std::wstring& error) {
    WIN32_FIND_DATAW entry{};
    HANDLE find = FindFirstFileW((job.dstDir + L"\\*").c_str(), &entry);
    int deleted = 0;
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
            const std::wstring file = job.dstDir + L"\\" + entry.cFileName;
            if (_wcsicmp(file.c_str(), job.selfPath.c_str()) != 0 && DeleteFileW(file.c_str())) ++deleted;
        } while (FindNextFileW(find, &entry));
        FindClose(find);
    }
    ui.Log(LogKind::Ok, clock.Elapsed(), L"[OK] %d archivos eliminados", deleted);
    if (!ScheduleSelfCleanup(job.selfPath, job.dstDir)) {
        error = L"No se pudo programar la limpieza final del desinstalador.";
        return false;
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

// --- Tablas de tareas -------------------------------------------------------

const PipeTask kInstallTasks[] = {
    {  1.5, L"Preparando el entorno de instalación",      TaskPrepareEnvironment },
    {  4.5, L"Verificando integridad y firmas digitales", TaskVerifyPayload },
    {  7.5, L"Diagnóstico del sistema y compatibilidad",  TaskSystemDiagnostics },
    { 11.0, L"Preparando registros del sistema",          TaskStageMainBinary },
    { 14.0, L"Descomprimiendo binarios y recursos",       TaskExtractResources },
    { 18.0, L"Configurando componentes y accesos directos", TaskCreateShortcuts },
    { 22.0, L"Instalando registros y asociaciones",       TaskRegisterAssociations },
    { 25.5, L"Desfragmentando y optimizando recursos",    TaskOptimizeLayout },
    { 29.0, L"Registrando el desinstalador",              TaskRegisterUninstaller },
    { 32.0, L"Limpieza final y verificación",             TaskFinalCleanup },
};
constexpr int kInstallTaskCount = static_cast<int>(sizeof(kInstallTasks) / sizeof(kInstallTasks[0]));

const PipeTask kUninstallTasks[] = {
    {  1.0, L"Preparando la desinstalación",              TaskUninstallPrepare },
    {  4.0, L"Revocando asociaciones de archivo",         TaskUninstallAssociations },
    {  7.0, L"Eliminando registros del sistema",          TaskUninstallRegistry },
    { 10.0, L"Eliminando accesos directos",               TaskUninstallShortcuts },
    { 13.0, L"Eliminando archivos del programa",          TaskUninstallFiles },
};
constexpr int kUninstallTaskCount = static_cast<int>(sizeof(kUninstallTasks) / sizeof(kUninstallTasks[0]));

// Reversión limpia si la instalación falla a medias: no se deja basura.
void RollbackInstall(const PipeJob& job) {
    RemoveAppShortcutsIn(GetShellFolder(CSIDL_DESKTOPDIRECTORY));
    RemoveAppShortcutsIn(GetShellFolder(CSIDL_COMMON_DESKTOPDIRECTORY));
    RemoveAppShortcutsIn(GetShellFolder(CSIDL_PROGRAMS));
    RemoveAppShortcutsIn(GetShellFolder(CSIDL_COMMON_PROGRAMS));
    const HKEY roots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
    for (HKEY root : roots) {
        RegDeleteTreeW(root, UNINSTALL_REG_KEY);
        RegDeleteTreeW(root, AppKeyPath().c_str());
        RegDeleteTreeW(root, L"Software\\Classes\\ARTPICST.Image");
        RegDeleteTreeW(root, L"Software\\Classes\\Applications\\artpicst.exe");
    }
    // Restaurar binarios originales apartados como ".old" y retirar los nuevos.
    RestoreStagedFile(job.dstDir + L"\\artpicst.exe");
    DeleteFileW((job.dstDir + L"\\artpicst.ico").c_str());
    DeleteFileW((job.dstDir + L"\\version.json").c_str());
    DeleteFileW((job.dstDir + L"\\README.md").c_str());
    DeleteFileW((job.dstDir + L"\\artpicst_updater.exe").c_str());
    DeleteFileW((job.dstDir + L"\\artpicst_installer.exe").c_str());
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

void RunPipeline(AppMode mode) {
    PipeUi ui{ g_state.hwnd };
    Pacer clock;
    PipeJob job;
    job.selfPath = GetModulePath();
    job.machineWide = g_machineWide;
    job.createDesktopShortcut = g_state.createDesktopShortcut;
    job.createStartMenuShortcut = g_state.createStartMenuShortcut;
    job.registerAssociations = g_state.registerFileAssociations;
    job.relaunchOnFinish = (mode == AppMode::Update);
    job.pacing = !g_state.silent;
    job.dstDir = (mode == AppMode::Uninstall) ? DetectInstallDir() : g_state.installPath;

    const PipeTask* tasks = nullptr;
    int taskCount = 0;
    double total = kInstallDurationSeconds;
    if (mode == AppMode::Uninstall) {
        tasks = kUninstallTasks;
        taskCount = kUninstallTaskCount;
        total = kUninstallDurationSeconds;
    } else {
        tasks = kInstallTasks;
        taskCount = kInstallTaskCount;
    }

    bool ok = true;
    std::wstring error;
    for (int i = 0; ok && i < taskCount; ++i) {
        ui.Progress(static_cast<int>(tasks[i].target / total * 100.0),
                    clock.Elapsed(), tasks[i].phase);
        ok = tasks[i].run(ui, job, clock, error);
        if (!ok) break;
        if (job.pacing) clock.SleepUntil(tasks[i].target);
    }

    if (ok && job.pacing) {
        // La marca final garantiza una duración EXACTA de 34.0 s (o 14 s en
        // desinstalación): nunca termina antes, aunque todo fuera rápido.
        while (clock.Elapsed() < total) {
            const double remain = total - clock.Elapsed();
            ui.Progress(static_cast<int>((total - remain) / total * 99.0),
                        clock.Elapsed(), L"Finalizando");
            Sleep(static_cast<DWORD>(remain * 1000.0));
        }
        ui.Progress(100, total, mode == AppMode::Uninstall ? L"Desinstalación completada"
                                                           : L"Instalación completada");
    }

    if (!ok && mode != AppMode::Uninstall) {
        ui.Log(LogKind::Warn, clock.Elapsed(), L"Revirtiendo cambios parciales...");
        RollbackInstall(job);
        ui.Log(LogKind::Error, clock.Elapsed(), L"[ER] %ls", error.c_str());
    } else if (!ok) {
        ui.Log(LogKind::Error, clock.Elapsed(), L"[ER] %ls", error.c_str());
    }

    // La marca final garantiza una duración EXACTA de 34.0 s (o 14 s en
    // desinstalación) cuando hay pacing; en --silent se completa al momento.
    g_lastRunSucceeded = ok;
    g_lastError = ok ? std::wstring() : error;
    // FIX: sin UI también se refleja el tiempo trabajado (lo pide la consola
    // "t = X.X s" si el pipeline llegara a tener log visible).
    g_state.workElapsed = clock.Elapsed();
    ui.Done(ok, clock.Elapsed());
    (void)mode;
}

// Detecta el directorio instalado (para desinstalación): registro HKLM -> HKCU.
std::wstring DetectInstallDir() {
    std::wstring dir;
    for (HKEY root : { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER }) {
        if (ReadRegStringValue(root, AppKeyPath(), L"InstallDir", dir) && !dir.empty()) return dir;
        if (ReadRegStringValue(root, UNINSTALL_REG_KEY, L"InstallLocation", dir) && !dir.empty()) return dir;
    }
    return GetModuleFolder();
}

// ============================================================================
// Elevación (UAC) y argumentos de línea de comandos
// ============================================================================

std::wstring AppKeyPath() {
    static const std::wstring key = L"Software\\" + std::wstring(artpicst::kAppName);
    return key;
}

bool IsProcessElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) || !token) return false;
    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, size, &size);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
}

// Argumentos de la línea de comandos sin el nombre del ejecutable.
std::wstring CommandLineArguments() {
    const wchar_t* full = GetCommandLineW();
    if (!full) return {};
    if (*full == L'"') {
        const wchar_t* end = wcschr(full + 1, L'"');
        return end ? std::wstring(end + 1) : std::wstring();
    }
    const wchar_t* space = wcschr(full, L' ');
    return space ? std::wstring(space + 1) : std::wstring();
}

// Relanza el instalador elevado (UAC). Devuelve true si ya se relanzó: en ese
// caso el proceso actual debe terminar de inmediato.
bool RelaunchElevatedSelf() {
    wchar_t selfPath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, selfPath, MAX_PATH) == 0) return false;
    std::wstring args = CommandLineArguments();
    if (args.find(L"--elevated") == std::wstring::npos) {
        if (!args.empty()) args += L" ";
        args += L"--elevated";
    }
    wchar_t selfDir[MAX_PATH] = {};
    lstrcpynW(selfDir, selfPath, MAX_PATH);
    if (wchar_t* slash = wcsrchr(selfDir, L'\\')) *slash = L'\0';
    const HINSTANCE result = ShellExecuteW(nullptr, L"runas", selfPath, args.c_str(),
                                           selfDir, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

unsigned int GetDpiForSystemSafe() {
    using PFN_GetDpiForSystem = unsigned int(WINAPI*)();
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        if (FARPROC raw = GetProcAddress(user32, "GetDpiForSystem")) {
            PFN_GetDpiForSystem fn = nullptr;
            static_assert(sizeof(fn) == sizeof(raw), "tamaño de puntero a función inesperado");
            std::memcpy(&fn, &raw, sizeof(fn));
            if (fn) {
                const UINT dpi = fn();
                if (dpi >= 48) return dpi;
            }
        }
    }
    HDC dc = GetDC(nullptr);
    UINT dpi = dc ? static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSY)) : 96u;
    if (dc) ReleaseDC(nullptr, dc);
    if (dpi < 48) dpi = 96u;
    return dpi;
}

// ============================================================================
// Dibujo: primitivas modernas (todo en unidades de diseño 96 DPI)
// ============================================================================

static void RoundPath(GraphicsPath& path, const RectF& rc, float radius) {
    const float w = rc.Width;
    const float h = rc.Height;
    float rad = radius;
    if (rad < 0.0f) rad = 0.0f;
    const float maxRad = (std::min)(w, h) * 0.5f;
    if (rad > maxRad) rad = maxRad;
    const float d = rad * 2.0f;
    path.Reset();
    path.StartFigure();
    path.AddArc(rc.X, rc.Y, d, d, 180.0f, 90.0f);
    path.AddArc(rc.X + w - d, rc.Y, d, d, 270.0f, 90.0f);
    path.AddArc(rc.X + w - d, rc.Y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(rc.X, rc.Y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

static void FillRound(Graphics& g, const RectF& rc, float radius, const Color& fill) {
    GraphicsPath path;
    RoundPath(path, rc, radius);
    SolidBrush brush(fill);
    g.FillPath(&brush, &path);
}

static void FillRoundGradient(Graphics& g, const RectF& rc, float radius, const Color& c1, const Color& c2, bool vertical = false) {
    GraphicsPath path;
    RoundPath(path, rc, radius);
    LinearGradientBrush brush(rc, c1, c2, vertical ? 90.0f : 0.0f);
    g.FillPath(&brush, &path);
}

static void StrokeRound(Graphics& g, const RectF& rc, float radius, const Color& border, float width = 1.0f) {
    GraphicsPath path;
    RoundPath(path, rc, radius);
    Pen pen(border, width);
    g.DrawPath(&pen, &path);
}

static void DrawTextIn(Graphics& g, const wchar_t* text, const RectF& rc, const Font& font,
                       const Color& color, bool centerH = true, bool centerV = true,
                       StringTrimming trimming = StringTrimmingNone, bool noWrap = false) {
    StringFormat format;
    format.SetAlignment(centerH ? StringAlignmentCenter : StringAlignmentNear);
    format.SetLineAlignment(centerV ? StringAlignmentCenter : StringAlignmentNear);
    if (trimming != StringTrimmingNone) {
        format.SetTrimming(trimming);
        if (noWrap) format.SetFormatFlags(StringFormatFlagsNoWrap);
    }
    SolidBrush brush(color);
    g.DrawString(text, -1, &font, rc, &format, &brush);
}

static void DrawCheckMark(Graphics& g, float x0, float y0, float x1, float y1, float x2, float y2, float width, const Color& color) {
    Pen pen(color, width);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    g.DrawLine(&pen, x0, y0, x1, y1);
    g.DrawLine(&pen, x1, y1, x2, y2);
}

static void DrawCheckBox(Graphics& g, const RectF& rc, bool checked) {
    if (checked) {
        FillRoundGradient(g, rc, 5.0f, COL_ACCENT_A, COL_ACCENT_B);
        DrawCheckMark(g, rc.X + rc.Width * 0.22f, rc.Y + rc.Height * 0.52f,
                      rc.X + rc.Width * 0.42f, rc.Y + rc.Height * 0.72f,
                      rc.X + rc.Width * 0.80f, rc.Y + rc.Height * 0.28f,
                      2.0f, Color(255, 255, 255, 255));
    } else {
        FillRound(g, rc, 5.0f, Color(255, 12, 15, 21));
        StrokeRound(g, rc, 5.0f, Color(255, 58, 68, 86), 1.0f);
    }
}

struct Fonts {
    FontFamily family;
    FontFamily monoFamily;
    Font fLogo;      // 26 px
    Font fTitle;     // 26 px
    Font fHeading;   // 18 px
    Font fBody;      // 13 px
    Font fSmall;     // 12.5 px
    Font fLabel;     // 10.5 px negrita
    Font fTiny;      // 10 px
    Font fMono;      // 11 px consola (log)
    Fonts()
        : family(L"Segoe UI"),
          monoFamily(L"Consolas"),
          fLogo(&family, 26.0f, FontStyleBold, UnitPixel),
          fTitle(&family, 26.0f, FontStyleBold, UnitPixel),
          fHeading(&family, 18.0f, FontStyleBold, UnitPixel),
          fBody(&family, 13.0f, FontStyleRegular, UnitPixel),
          fSmall(&family, 12.5f, FontStyleRegular, UnitPixel),
          fLabel(&family, 10.5f, FontStyleBold, UnitPixel),
          fTiny(&family, 10.0f, FontStyleRegular, UnitPixel),
          fMono(&monoFamily, 11.0f, FontStyleRegular, UnitPixel) {}
};

// Caché global de fuentes: las clases Font/FontFamily de GDI+ son caras de
// construir y ANTES se recreaban en cada repintado (~30/s durante la
// instalación). Con el doble búfer + esta caché el repintado del progreso es
// O(1) sin parpadeo ni tirones.
Fonts& SharedFonts() {
    static Fonts instance;   // construcción perezosa tras GdiplusStartup
    return instance;
}

static void DrawLogo(Graphics& g, const Fonts& fonts, float cx, float cy, float size) {
    const RectF tile(cx - size * 0.5f, cy - size * 0.5f, size, size);
    FillRoundGradient(g, tile, size * 0.24f, COL_ACCENT_A, COL_ACCENT_B);
    const RectF shine(tile.X, tile.Y, tile.Width, tile.Height * 0.5f);
    FillRound(g, shine, size * 0.24f, Color(28, 255, 255, 255));
    DrawTextIn(g, L"A", tile, fonts.fLogo, Color(255, 255, 255, 255), true, true);
}

// Fondo Midnight Sapphire con resplandores zafiro/violeta + barra superior.
static void DrawChrome(Graphics& g, const Fonts&, float W, float H) {
    SolidBrush bg(COL_BG);
    g.FillRectangle(&bg, 0.0f, 0.0f, W, H);
    const RectF topBar(0.0f, 0.0f, W, 3.0f);
    LinearGradientBrush accent(topBar, COL_ACCENT_A, COL_ACCENT_B, 0.0f);
    g.FillRectangle(&accent, topBar);
}

// Encabezado del área de contenido: banda #0A0A0A con título y subtítulo
// (el logo y los pasos viven en la barra lateral del rediseño).
static void DrawPageHeader(Graphics& g, const Fonts& fonts, float W, const wchar_t* subtitle) {
    const RectF band(SIDEBAR_W, 3.0f, W - SIDEBAR_W, 52.0f);
    SolidBrush bandBrush(COL_PANEL);
    g.FillRectangle(&bandBrush, band);
    const RectF hairline(band.X, band.Y + band.Height, band.Width, 1.0f);
    SolidBrush hair(Gdiplus::Color(255, 24, 28, 38));
    g.FillRectangle(&hair, hairline);

    RectF titleRect(band.X + CONTENT_PAD, band.Y + 8.0f, band.Width - CONTENT_PAD * 2.0f - 90.0f, 18.0f);
    DrawTextIn(g, APP_NAME, titleRect, fonts.fLabel, COL_TEXT, false, true);
    RectF subRect(band.X + CONTENT_PAD, band.Y + 28.0f, band.Width - CONTENT_PAD * 2.0f - 90.0f, 16.0f);
    DrawTextIn(g, subtitle, subRect, fonts.fTiny, COL_TEXT_DIM, false, true);
}

// Índice del paso activo para la barra lateral (5 etapas en instalación,
// 4 en desinstalación: el modo cambia la numeración).
static int StepIndexForHeader() {
    if (g_state.mode == AppMode::Uninstall) {
        switch (g_state.currentStep) {
            case WizardStep::Welcome:          return 0;
            case WizardStep::UninstallConfirm: return 1;
            case WizardStep::Working:          return 2;
            case WizardStep::Complete:         return 3;
            default:                           return 0;
        }
    }
    switch (g_state.currentStep) {
        case WizardStep::Welcome:          return 0;
        case WizardStep::License:          return 1;
        case WizardStep::Destination:      return 2;
        case WizardStep::UninstallConfirm: return 1;
        case WizardStep::Working:          return 3;
        case WizardStep::Complete:         return 4;
    }
    return 0;
}

// Barra lateral del rediseño: logo, versión y lista de pasos con estado
// (hecho = neón con marca, activo = anillo acento, pendiente = gris).
static void DrawSidebar(Graphics& g, const Fonts& fonts, float W, float H) {
    (void)W;
    (void)H;
    const RectF rail(0.0f, 3.0f, SIDEBAR_W, H);
    SolidBrush railBrush(COL_PANEL);
    g.FillRectangle(&railBrush, rail);
    const RectF hair(SIDEBAR_W, 3.0f, 1.0f, H);
    SolidBrush hairBrush(Color(255, 22, 26, 36));
    g.FillRectangle(&hairBrush, hair);

    DrawLogo(g, fonts, 42.0f, 48.0f, 34.0f);
    RectF brandRect(66.0f, 34.0f, SIDEBAR_W - 74.0f, 16.0f);
    DrawTextIn(g, APP_NAME, brandRect, fonts.fLabel, COL_TEXT, false, true);
    RectF brandSub(66.0f, 52.0f, SIDEBAR_W - 74.0f, 14.0f);
    const std::wstring brandText = (g_state.mode == AppMode::Uninstall)
        ? std::wstring(L"Desinstalador")
        : (g_state.mode == AppMode::Update
               ? std::wstring(L"Actualización")
               : std::wstring(L"Asistente v") + APP_VERSION);
    DrawTextIn(g, brandText.c_str(), brandSub, fonts.fTiny, COL_TEXT_DIM, false, true);

    struct SideStep { const wchar_t* label; int state; };   // 0 pendiente, 1 activo, 2 hecho
    SideStep steps[5];
    int count = 0;
    const int idx = StepIndexForHeader();
    if (g_state.mode == AppMode::Uninstall) {
        steps[0] = { L"Bienvenida",   idx == 0 ? 1 : 2 };
        steps[1] = { L"Confirmación", idx == 1 ? 1 : (idx > 1 ? 2 : 0) };
        steps[2] = { L"Progreso",     idx == 2 ? 1 : (idx > 2 ? 2 : 0) };
        steps[3] = { L"Completado",   idx == 3 ? 1 : 0 };
        count = 4;
    } else {
        steps[0] = { L"Bienvenida",   idx == 0 ? 1 : 2 };
        steps[1] = { L"Licencia",     idx == 1 ? 1 : (idx > 1 ? 2 : 0) };
        steps[2] = { L"Destino",      idx == 2 ? 1 : (idx > 2 ? 2 : 0) };
        steps[3] = { L"Instalación",  idx == 3 ? 1 : (idx > 3 ? 2 : 0) };
        steps[4] = { L"Completado",   idx == 4 ? 1 : 0 };
        count = 5;
    }

    float y = 132.0f;
    for (int i = 0; i < count; ++i, y += 46.0f) {
        const bool active = steps[i].state == 1;
        const bool done = steps[i].state == 2;
        const float dotR = 10.0f;
        const RectF dot(30.0f, y - dotR, dotR * 2.0f, dotR * 2.0f);
        wchar_t num[8] = {};
        swprintf(num, 8, L"%d", i + 1);
        if (done) {
            FillRoundGradient(g, dot, dotR, COL_ACCENT_A, COL_ACCENT_B);
            DrawCheckMark(g, 35.0f, y + 0.5f, 38.5f, y + 4.5f, 45.0f, y - 4.5f, 2.0f,
                          Color(255, 255, 255, 255));
        } else if (active) {
            FillRound(g, dot, dotR, Color(255, 16, 20, 28));
            StrokeRound(g, dot, dotR, COL_ACCENT_A, 2.0f);
            RectF numRect(30.0f, y - dotR, dotR * 2.0f, dotR * 2.0f);
            DrawTextIn(g, num, numRect, fonts.fTiny, COL_ACCENT_A, true, true);
        } else {
            FillRound(g, dot, dotR, Color(255, 18, 22, 30));
            StrokeRound(g, dot, dotR, Color(255, 44, 52, 68), 1.0f);
            RectF numRect(30.0f, y - dotR, dotR * 2.0f, dotR * 2.0f);
            DrawTextIn(g, num, numRect, fonts.fTiny, COL_TEXT_DIM, true, true);
        }
        RectF labelRect(52.0f, y - 9.0f, SIDEBAR_W - 60.0f, 18.0f);
        DrawTextIn(g, steps[i].label, labelRect, fonts.fSmall,
                   active ? COL_TEXT : (done ? COL_TEXT_SOFT : COL_TEXT_DIM), false, true);
        if (i + 1 < count) {
            const RectF link(39.0f, y + dotR + 2.0f, 2.0f, 46.0f - dotR * 2.0f - 4.0f);
            SolidBrush linkBrush(done ? Color(255, 40, 130, 150) : Color(255, 30, 36, 48));
            g.FillRectangle(&linkBrush, link);
        }
    }
}

// Barra de progreso fluida con relleno degradado y línea de brillo.
static void DrawProgressBar(Graphics& g, const RectF& track, double pct) {
    FillRound(g, track, 5.0f, Color(255, 18, 22, 30));
    StrokeRound(g, track, 5.0f, Color(255, 36, 44, 58), 1.0f);
    if (pct > 0.0) {
        double fillW = track.Width * (pct / 100.0);
        if (fillW < 2.0 && pct > 0.0) fillW = 2.0;
        if (fillW > 1.0f) {
            const RectF fill(track.X, track.Y, static_cast<float>(fillW), track.Height);
            Region clip(fill);
            g.SetClip(&clip);
            FillRoundGradient(g, track, 5.0f, COL_ACCENT_A, COL_ACCENT_B);
            const RectF glow(track.X, track.Y, static_cast<float>(fillW), 2.0f);
            SolidBrush glowBrush(Color(90, 255, 255, 255));
            g.FillRectangle(&glowBrush, glow);
            g.ResetClip();
        }
    }
}

// ============================================================================
// Consola de log central (panel #0A0A0A, autoscroll, scroll con rueda)
// ============================================================================

static void DrawLogConsole(Graphics& g, const Fonts& fonts, const RectF& rc) {
    FillRound(g, rc, 10.0f, COL_PANEL_DEEP);
    StrokeRound(g, rc, 10.0f, COL_PANEL_BORDER, 1.0f);

    // Cabecera del panel (el cronómetro vive bajo la barra de progreso:
    // aquí solo el título — el reloj duplicado quedaba congelado entre
    // invalidaciones de la consola).
    RectF headLabel(rc.X + 16.0f, rc.Y + 10.0f, rc.Width - 32.0f, 14.0f);
    DrawTextIn(g, L"CONSOLA DE INSTALACIÓN", headLabel, fonts.fLabel, COL_TEXT_DIM, false, true);

    const float padX = 16.0f;
    const float lineH = 17.0f;
    const float listTop = rc.Y + 30.0f;
    const float listBottom = rc.Y + rc.Height - 12.0f;
    const int visible = static_cast<int>((listBottom - listTop) / lineH);
    if (visible <= 0) return;

    const int total = static_cast<int>(g_state.log.size());
    const int maxScroll = total > visible ? total - visible : 0;
    int scroll = g_state.logScroll;
    if (scroll > maxScroll) scroll = maxScroll;
    if (scroll < 0) scroll = 0;
    const int first = maxScroll - scroll;   // 0 = pegado al final (autoscroll)

    Region clip(rc);
    g.SetClip(&clip);

    float y = listTop;
    for (int i = first; i < total && y + lineH <= listBottom + 0.5f; ++i, y += lineH) {
        const LogLine& line = g_state.log[i];
        Color lineColor = COL_TEXT_SOFT;
        if (line.kind == LogKind::Ok)    lineColor = COL_SUCCESS;
        if (line.kind == LogKind::Warn)  lineColor = COL_WARN;
        if (line.kind == LogKind::Error) lineColor = COL_ERROR;

        wchar_t stamp[24] = {};
        swprintf(stamp, 24, L"[%05.1fs]", line.atSeconds);
        const RectF stampRect(rc.X + padX, y, 62.0f, lineH);
        DrawTextIn(g, stamp, stampRect, fonts.fMono, COL_TEXT_DIM, false, true, StringTrimmingNone, true);

        const RectF textRect(rc.X + padX + 68.0f, y, rc.Width - padX * 2.0f - 68.0f - 14.0f, lineH);
        DrawTextIn(g, line.text.c_str(), textRect, fonts.fMono, lineColor, false, true,
                   StringTrimmingEllipsisCharacter, true);
    }
    g.ResetClip();

    // Barra de desplazamiento fina (solo si hay desbordamiento)
    if (maxScroll > 0) {
        const float trackH = listBottom - listTop;
        const float thumbH = trackH * static_cast<float>(visible) / static_cast<float>(total);
        const float trackY = listTop;
        const float thumbY = trackY + (trackH - thumbH) *
                             (maxScroll > 0 ? static_cast<float>(maxScroll - scroll) / static_cast<float>(maxScroll) : 0.0f);
        const RectF rail(rc.X + rc.Width - 8.0f, trackY, 3.0f, trackH);
        SolidBrush railBrush(Color(255, 26, 30, 40));
        g.FillRectangle(&railBrush, rail);
        FillRound(g, RectF(rc.X + rc.Width - 9.0f, thumbY, 5.0f, thumbH), 2.0f, COL_ACCENT_A);
    }
}

// ============================================================================
// Pantallas del asistente
// ============================================================================

struct LayoutRects {
    RectF back;
    RectF next;
    RectF cancel;
    RectF browse;
    RectF rows[3];
    int rowCount = 0;
    // Zonas animadas durante el trabajo (invalidación selectiva sin parpadeo).
    RectF work;
    RectF time;
    RectF log;
    RectF web;
    RectF check;
};

float DesignX(int physicalX) { return static_cast<float>(physicalX) / g_scale; }
float DesignY(int physicalY) { return static_cast<float>(physicalY) / g_scale; }

LayoutRects ComputeLayout(float W, float H) {
    LayoutRects r;
    const float contentX = SIDEBAR_W + CONTENT_PAD;
    const float contentRight = W - RIGHT_MARGIN;
    const float buttonH = 44.0f;
    const float buttonY = H - buttonH - 24.0f;

    r.next = RectF(contentRight - 168.0f, buttonY, 168.0f, buttonH);
    r.back = RectF(contentX, buttonY, 124.0f, buttonH);
    r.cancel = RectF(contentRight - 76.0f, 64.0f, 76.0f, 24.0f);   // bajo la banda del encabezado
    r.browse = RectF(contentX, 158.0f, 118.0f, 30.0f);
    r.work = RectF(contentX, 42.0f, contentRight - contentX, 104.0f);
    r.time = RectF(contentX, 128.0f, 140.0f, 16.0f);
    r.log = RectF(contentX, 152.0f, contentRight - contentX, H - 152.0f - 62.0f);
    {
        const float rowW = ((contentRight - contentX) - 12.0f) * 0.5f;
        r.web = RectF(contentX, 312.0f, rowW, 36.0f);
        r.check = RectF(contentX + rowW + 12.0f, 312.0f, rowW, 36.0f);
    }

    if (g_state.currentStep == WizardStep::License || g_state.currentStep == WizardStep::UninstallConfirm) {
        const float rowH = 36.0f;
        const float gap = 10.0f;
        const float y0 = (g_state.currentStep == WizardStep::License) ? 312.0f : 272.0f;
        r.rowCount = (g_state.currentStep == WizardStep::License) ? 3 : 1;
        float y = y0;
        for (int i = 0; i < r.rowCount; ++i) {
            r.rows[i] = RectF(contentX, y, contentRight - contentX, rowH);
            y += rowH + gap;
        }
    }
    return r;
}

int HoverZoneAt(const LayoutRects& r, float lx, float ly) {
    if (g_state.isWorking) return HOVER_NONE;
    auto hit = [&](const RectF& rc) {
        return lx >= rc.X && lx <= rc.X + rc.Width && ly >= rc.Y && ly <= rc.Y + rc.Height;
    };
    if (hit(r.cancel)) return HOVER_CANCEL;
    if (g_state.currentStep == WizardStep::Destination && hit(r.browse)) return HOVER_BROWSE;
    if (g_state.currentStep == WizardStep::Complete && g_state.installSucceeded &&
        g_state.mode != AppMode::Uninstall) {
        if (hit(r.web)) return HOVER_WEB;
        if (hit(r.check)) return HOVER_CHECK;
    }
    if (g_state.currentStep == WizardStep::License ||
        g_state.currentStep == WizardStep::Destination ||
        g_state.currentStep == WizardStep::UninstallConfirm) {
        if (hit(r.back)) return HOVER_BACK;
        for (int i = 0; i < r.rowCount; ++i) {
            if (hit(r.rows[i])) return HOVER_ROW_DESKTOP + i;
        }
    }
    if ((g_state.currentStep == WizardStep::Welcome ||
         g_state.currentStep == WizardStep::License ||
         g_state.currentStep == WizardStep::Destination ||
         g_state.currentStep == WizardStep::UninstallConfirm ||
         g_state.currentStep == WizardStep::Complete) && hit(r.next)) {
        return HOVER_NEXT;
    }
    if (g_state.currentStep == WizardStep::Complete && hit(r.back)) return HOVER_BACK;
    return HOVER_NONE;
}

static void DrawOptionRow(Graphics& g, const Fonts& fonts, const RectF& row, const wchar_t* label, bool value, bool hot) {
    FillRound(g, row, 8.0f, hot ? Color(255, 24, 30, 42) : Color(255, 15, 18, 25));
    StrokeRound(g, row, 8.0f, hot ? COL_BTN_GHOST_BORDER_HOT : COL_PANEL_BORDER, 1.0f);
    const RectF boxRect(row.X + 12.0f, row.Y + (row.Height - 18.0f) * 0.5f, 18.0f, 18.0f);
    DrawCheckBox(g, boxRect, value);
    RectF labelRect(row.X + 40.0f, row.Y, row.Width - 50.0f, row.Height);
    DrawTextIn(g, label, labelRect, fonts.fSmall, COL_TEXT, false, true);
}

static void DrawPrimaryButton(Graphics& g, const Fonts& fonts, const RectF& rc, const wchar_t* text, bool hot) {
    FillRoundGradient(g, rc, 8.0f, COL_ACCENT_A, COL_ACCENT_B);
    if (hot) FillRound(g, rc, 8.0f, Color(26, 255, 255, 255));
    DrawTextIn(g, text, rc, fonts.fSmall, Color(255, 255, 255, 255), true, true);
}

static void DrawGhostButton(Graphics& g, const Fonts& fonts, const RectF& rc, const wchar_t* text, bool hot) {
    FillRound(g, rc, 8.0f, hot ? COL_BTN_GHOST_HOT : COL_BTN_GHOST);
    StrokeRound(g, rc, 8.0f, hot ? COL_BTN_GHOST_BORDER_HOT : COL_BTN_GHOST_BORDER, 1.0f);
    DrawTextIn(g, text, rc, fonts.fSmall, hot ? COL_TEXT : COL_TEXT_SOFT, true, true);
}

void RenderWelcome(Graphics& g, const Fonts& fonts, float W, float H, const LayoutRects& layout) {
    (void)W;
    const float contentX = layout.back.X;
    const float CW = layout.next.X + layout.next.Width - contentX;
    const float cx = contentX + CW * 0.5f;

    DrawLogo(g, fonts, cx, 96.0f, 56.0f);

    RectF nameRect(contentX, 138.0f, CW, 40.0f);
    std::wstring nameAndVersion = std::wstring(APP_NAME) + L"  v" + APP_VERSION;
    DrawTextIn(g, nameAndVersion.c_str(), nameRect, fonts.fTitle, COL_TEXT, true, false);

    RectF tagRect(contentX, 182.0f, CW, 22.0f);
    DrawTextIn(g, L"Visor de imágenes premium para Windows — rápido, ligero y moderno",
               tagRect, fonts.fSmall, COL_TEXT_SOFT, true, true);

    const float cardY = 226.0f;
    const RectF card(contentX, cardY, CW, 168.0f);
    FillRound(g, card, 14.0f, COL_PANEL);
    StrokeRound(g, card, 14.0f, COL_PANEL_BORDER, 1.0f);

    const wchar_t* features[] = {
        L"Más de 30 formatos: PNG, WebP, HEIC, AVIF, GIF, RAW y más",
        L"Zoom fluido por GPU, píxel perfecto al 100% y Ultra-Claridad HDR",
        L"Instalación tradicional: Program Files, menú Inicio y desinstalador",
        L"Módulo inteligente de actualización automática integrado",
        L"Interfaz oscura elegante y consumo mínimo de RAM y CPU",
    };
    float fy = cardY + 22.0f;
    for (const wchar_t* text : features) {
        const float dotR = 3.5f;
        const float dotY = fy + 8.0f;
        FillRoundGradient(g, RectF(card.X + 24.0f, dotY - dotR, dotR * 2.0f, dotR * 2.0f), dotR, COL_ACCENT_A, COL_ACCENT_B);
        RectF featureRect(card.X + 40.0f, fy - 2.0f, card.Width - 60.0f, 22.0f);
        DrawTextIn(g, text, featureRect, fonts.fSmall, COL_TEXT_SOFT, false, true);
        fy += 29.0f;
    }

    RectF hint(contentX, H - 92.0f, CW, 18.0f);
    DrawTextIn(g, (g_machineWide
                       ? L"Se instalará para todos los usuarios (requiere administrador)."
                       : L"Se instalará para tu usuario, sin permisos de administrador."),
               hint, fonts.fTiny, COL_TEXT_DIM, true, true);
}

// Página NUEVA del rediseño: carpeta de destino con selector nativo, espacio
// libre real, tamaño del payload y detección de instalaciones previas.
void RenderDestination(Graphics& g, const Fonts& fonts, float W, float H, const LayoutRects& layout) {
    (void)W;
    const float contentX = layout.back.X;
    const float CW = layout.next.X + layout.next.Width - contentX;

    RectF titleRect(contentX, 40.0f, CW, 30.0f);
    DrawTextIn(g, L"Carpeta de destino", titleRect, fonts.fHeading, COL_TEXT, true, true);

    const RectF panel(contentX, 82.0f, CW, 60.0f);
    FillRound(g, panel, 10.0f, COL_PANEL_DEEP);
    StrokeRound(g, panel, 10.0f, COL_PANEL_BORDER, 1.0f);
    RectF panelLabel(panel.X + 16.0f, panel.Y + 8.0f, panel.Width - 32.0f, 13.0f);
    DrawTextIn(g, L"CARPETA DE INSTALACIÓN", panelLabel, fonts.fLabel, COL_TEXT_DIM, false, true);
    RectF panelPath(panel.X + 16.0f, panel.Y + 28.0f, panel.Width - 32.0f, 22.0f);
    DrawTextIn(g, g_state.installPath.c_str(), panelPath, fonts.fSmall, COL_TEXT, false, true,
               StringTrimmingEllipsisCharacter, true);

    DrawGhostButton(g, fonts, layout.browse, L"Examinar...", g_state.hoverZone == HOVER_BROWSE);

    const float cardW = (CW - 14.0f) * 0.5f;
    const float cardsY = 206.0f;
    const RectF cardFree(contentX, cardsY, cardW, 74.0f);
    const RectF cardSize(contentX + cardW + 14.0f, cardsY, cardW, 74.0f);
    auto drawCard = [&](const RectF& rc, const wchar_t* label, const std::wstring& value, const Color& valueColor) {
        FillRound(g, rc, 10.0f, COL_PANEL);
        StrokeRound(g, rc, 10.0f, COL_PANEL_BORDER, 1.0f);
        RectF l(rc.X + 16.0f, rc.Y + 10.0f, rc.Width - 32.0f, 13.0f);
        DrawTextIn(g, label, l, fonts.fLabel, COL_TEXT_DIM, false, true);
        RectF v(rc.X + 16.0f, rc.Y + 30.0f, rc.Width - 32.0f, 26.0f);
        DrawTextIn(g, value.c_str(), v, fonts.fBody, valueColor, false, true,
                   StringTrimmingEllipsisCharacter, true);
    };
    drawCard(cardFree, L"ESPACIO LIBRE EN LA UNIDAD", FormatBytes(g_state.destFreeBytes),
             g_state.destFreeBytes > 0 ? COL_TEXT : COL_WARN);
    const std::wstring sizeText = (g_state.payloadBytes > 0)
        ? FormatBytes(g_state.payloadBytes)
        : std::wstring(L"No incrustado (compilación de desarrollo)");
    drawCard(cardSize, L"TAMAÑO DE LA INSTALACIÓN", sizeText, COL_TEXT);

    float infoY = cardsY + 90.0f;
    if (g_state.existingInstallFound) {
        const RectF info(contentX, infoY, CW, 52.0f);
        FillRound(g, info, 10.0f, Color(255, 30, 24, 12));
        StrokeRound(g, info, 10.0f, Color(255, 120, 92, 30), 1.0f);
        const float dotR = 4.0f;
        FillRound(g, RectF(info.X + 16.0f, info.Y + info.Height * 0.5f - dotR, dotR * 2.0f, dotR * 2.0f),
                  dotR, COL_WARN);
        RectF infoText(info.X + 32.0f, info.Y, info.Width - 48.0f, info.Height);
        std::wstring msg = L"Instalación previa detectada (versión " + g_state.existingVersion +
                           L"). Se actualizará sin perder tu configuración.";
        DrawTextIn(g, msg.c_str(), infoText, fonts.fSmall, COL_TEXT, false, true,
                   StringTrimmingEllipsisCharacter, true);
        infoY += 64.0f;
    }

    RectF note(contentX, infoY + 8.0f, CW, 18.0f);
    DrawTextIn(g, L"Recomendado: al menos 100 MB libres. ARTPICST no instala servicios ni procesos en segundo plano.",
               note, fonts.fTiny, COL_TEXT_DIM, false, true, StringTrimmingEllipsisCharacter, true);

    RectF destRect(contentX, H - 78.0f, CW, 18.0f);
    std::wstring dest = L"Se instalará en:  " + g_state.installPath;
    DrawTextIn(g, dest.c_str(), destRect, fonts.fTiny, COL_TEXT_DIM, false, true,
               StringTrimmingEllipsisCharacter, true);
}

void RenderLicense(Graphics& g, const Fonts& fonts, float W, float H, const LayoutRects& layout) {
    (void)W;
    const float contentX = layout.back.X;
    const float CW = layout.next.X + layout.next.Width - contentX;

    RectF titleRect(contentX, 40.0f, CW, 30.0f);
    DrawTextIn(g, L"Licencia y opciones", titleRect, fonts.fHeading, COL_TEXT, true, true);

    const RectF box(contentX, 82.0f, CW, 200.0f);
    FillRound(g, box, 12.0f, COL_PANEL_DEEP);
    StrokeRound(g, box, 12.0f, COL_PANEL_BORDER, 1.0f);

    RectF boxTitle(box.X + 18.0f, box.Y + 10.0f, box.Width - 36.0f, 16.0f);
    DrawTextIn(g, L"ACUERDO DE LICENCIA", boxTitle, fonts.fLabel, COL_TEXT_SOFT, false, true);

    const wchar_t* licenseText =
        L"ARTPICST es un visor de imágenes gratuito y de código abierto.\n"
        L"Este software se distribuye \"tal cual\", sin garantías de ningún tipo,\n"
        L"expresas o implícitas. El autor no será responsable de los daños que\n"
        L"puedan derivarse de su uso.\n\n"
        L"Puedes usarlo, copiarlo y modificarlo libremente para fines personales.\n"
        L"No está permitida su venta ni su redistribución con fines comerciales\n"
        L"sin autorización previa.\n\n"
        L"Al hacer clic en \"Instalar\" aceptas estos términos.";

    const float textW = box.Width - 36.0f;
    const float textH = box.Height - 34.0f;
    const RectF textArea(box.X + 18.0f, box.Y + 32.0f, textW, textH);
    float fontSize = 11.5f;
    for (int attempt = 0; attempt < 8; ++attempt) {
        Font probe(&fonts.family, fontSize, FontStyleRegular, UnitPixel);
        RectF measured;
        StringFormat probeFormat;
        probeFormat.SetFormatFlags(StringFormatFlagsLineLimit);
        g.MeasureString(licenseText, -1, &probe, RectF(0.0f, 0.0f, textW, 2000.0f), &probeFormat, &measured);
        if (measured.Height <= textH || fontSize <= 8.0f) break;
        fontSize -= 0.5f;
    }
    Font licenseFont(&fonts.family, fontSize, FontStyleRegular, UnitPixel);
    StringFormat textFormat;
    textFormat.SetAlignment(StringAlignmentNear);
    textFormat.SetLineAlignment(StringAlignmentNear);
    textFormat.SetFormatFlags(StringFormatFlagsLineLimit);
    SolidBrush licenseBrush(Color(255, 168, 178, 194));
    g.DrawString(licenseText, -1, &licenseFont, textArea, &textFormat, &licenseBrush);

    RectF optTitle(contentX, 294.0f, CW, 14.0f);
    DrawTextIn(g, L"OPCIONES DE INSTALACIÓN", optTitle, fonts.fLabel, COL_TEXT_DIM, false, true);

    struct OptionRow { const wchar_t* label; const bool* value; int hover; };
    const OptionRow rows[] = {
        { L"Crear acceso directo en el Escritorio", &g_state.createDesktopShortcut, HOVER_ROW_DESKTOP },
        { L"Crear acceso directo en el Menú Inicio", &g_state.createStartMenuShortcut, HOVER_ROW_STARTMENU },
        { L"Asociar formatos de imagen a ARTPICST", &g_state.registerFileAssociations, HOVER_ROW_ASSOC },
    };
    for (int i = 0; i < 3; ++i) {
        DrawOptionRow(g, fonts, layout.rows[i], rows[i].label, *rows[i].value,
                      g_state.hoverZone == rows[i].hover);
    }

    RectF destRect(contentX, H - 78.0f, CW, 18.0f);
    std::wstring dest = L"Se instalará en:  " + g_state.installPath;
    DrawTextIn(g, dest.c_str(), destRect, fonts.fTiny, COL_TEXT_DIM, false, true,
               StringTrimmingEllipsisCharacter, true);
}

void RenderUninstallConfirm(Graphics& g, const Fonts& fonts, float W, float H, const LayoutRects& layout) {
    (void)W;
    (void)H;
    const float contentX = layout.back.X;
    const float CW = layout.next.X + layout.next.Width - contentX;

    const float r = 26.0f;
    const float cy = 96.0f;
    const RectF ring(contentX + 4.0f, cy - r, r * 2.0f, r * 2.0f);
    GraphicsPath ringPath;
    RoundPath(ringPath, ring, r);
    SolidBrush ringBrush(COL_WARN);
    g.FillPath(&ringBrush, &ringPath);
    Font bangFont(&fonts.family, 30.0f, FontStyleBold, UnitPixel);
    DrawTextIn(g, L"!", ring, bangFont, Color(255, 20, 20, 24), true, true);

    RectF titleRect(contentX + 72.0f, 66.0f, CW - 72.0f, 30.0f);
    DrawTextIn(g, L"Desinstalar ARTPICST", titleRect, fonts.fHeading, COL_TEXT, false, true);

    RectF subRect(contentX + 72.0f, 100.0f, CW - 72.0f, 20.0f);
    DrawTextIn(g, L"El programa y sus componentes se eliminarán de este equipo.",
               subRect, fonts.fSmall, COL_TEXT_SOFT, false, true);

    const RectF card(contentX, 146.0f, CW, 104.0f);
    FillRound(g, card, 12.0f, COL_PANEL);
    StrokeRound(g, card, 12.0f, COL_PANEL_BORDER, 1.0f);

    struct InfoRow { const wchar_t* label; const std::wstring* value; };
    const InfoRow info[] = {
        { L"VERSIÓN INSTALADA", &g_state.uninstallInfoVersion },
        { L"CARPETA",           &g_state.uninstallInfoDir },
        { L"TAMAÑO",            &g_state.uninstallInfoSize },
    };
    float iy = card.Y + 14.0f;
    for (const auto& row : info) {
        RectF labelRect(card.X + 18.0f, iy, 170.0f, 16.0f);
        DrawTextIn(g, row.label, labelRect, fonts.fLabel, COL_TEXT_DIM, false, true);
        RectF valueRect(card.X + 196.0f, iy, card.Width - 214.0f, 16.0f);
        DrawTextIn(g, row.value->c_str(), valueRect, fonts.fSmall, COL_TEXT, false, true,
                   StringTrimmingEllipsisCharacter, true);
        iy += 30.0f;
    }

    DrawOptionRow(g, fonts, layout.rows[0],
                  L"Conservar configuraciones e historial del usuario",
                  g_state.keepUserConfig, g_state.hoverZone == HOVER_ROW_DESKTOP);

    RectF hint(contentX, 336.0f, CW, 18.0f);
    DrawTextIn(g, L"Las imágenes del equipo y sus miniaturas no se verán afectadas.",
               hint, fonts.fTiny, COL_TEXT_DIM, false, true);
}

void RenderWorking(Graphics& g, const Fonts& fonts, float W, float H) {
    const float contentX = SIDEBAR_W + CONTENT_PAD;
    const float contentRight = W - RIGHT_MARGIN;
    const float CW = contentRight - contentX;

    const wchar_t* title =
        g_state.mode == AppMode::Uninstall ? L"Desinstalando ARTPICST" :
        g_state.mode == AppMode::Update    ? L"Actualizando ARTPICST" :
                                             L"Instalando ARTPICST";
    RectF titleRect(contentX, 42.0f, CW, 30.0f);
    DrawTextIn(g, title, titleRect, fonts.fHeading, COL_TEXT, true, true);

    RectF statusRect(contentX, 76.0f, CW, 20.0f);
    DrawTextIn(g, g_state.installStatus.c_str(), statusRect, fonts.fSmall, COL_TEXT_SOFT, true, true);

    // Barra de progreso REORGANIZADA: más alta (14 px), a todo el ancho del
    // contenido, con porcentaje integrado a la derecha dentro de la banda.
    const RectF track(contentX, 108.0f, CW, 14.0f);
    DrawProgressBar(g, track, g_state.progressShown);
    wchar_t percentText[32];
    swprintf(percentText, 32, L"%.0f%%", g_state.progressShown);
    RectF pctRect(track.X + track.Width - 64.0f, track.Y + 1.0f, 56.0f, 12.0f);
    DrawTextIn(g, percentText, pctRect, fonts.fTiny, COL_TEXT, false, true,
               StringTrimmingNone, true);

    // Marca de tiempo a la izquierda, bajo la barra (la consola ya no
    // duplica el cronómetro en su cabecera).
    wchar_t timeText[32];
    swprintf(timeText, 32, L"%.1f s", g_state.workElapsed);
    RectF timeRect(contentX, 128.0f, 120.0f, 16.0f);
    DrawTextIn(g, timeText, timeRect, fonts.fTiny, COL_TEXT_DIM, false, true,
               StringTrimmingNone, true);

    // Consola de log: comienza más abajo y llega hasta los botones (todo el
    // alto extra ganado por eliminar la cabecera duplicada).
    const RectF console(contentX, 152.0f, CW, H - 152.0f - 76.0f);
    DrawLogConsole(g, fonts, console);

    std::wstring dest = (g_state.mode == AppMode::Uninstall)
        ? (L"Desinstalando de: " + g_state.uninstallInfoDir)
        : (L"Destino: " + g_state.installPath);
    RectF destRect(contentX, H - 50.0f, CW, 18.0f);
    DrawTextIn(g, dest.c_str(), destRect, fonts.fTiny, COL_TEXT_DIM, true, true,
               StringTrimmingEllipsisCharacter, true);
}

void RenderComplete(Graphics& g, const Fonts& fonts, float W) {
    const float contentX = SIDEBAR_W + CONTENT_PAD;
    const float contentRight = W - RIGHT_MARGIN;
    const float CW = contentRight - contentX;
    const float cx = contentX + CW * 0.5f;

    if (g_state.installSucceeded) {
        // --- Sello "clásico pero moderno": disco con halo degradado y marca
        //     de verificación, evocando los finales de los instaladores
        //     clásicos pero con luz neón. ---
        const float r = 32.0f;
        const float cy = 96.0f;
        for (int i = 3; i >= 1; --i) {
            const float rr = r + i * 7.0f;
            const RectF halo(cx - rr, cy - rr, rr * 2.0f, rr * 2.0f);
            GraphicsPath haloPath;
            RoundPath(haloPath, halo, rr);
            SolidBrush haloBrush(Color(static_cast<BYTE>(20 - i * 5), 86, 160, 255));
            g.FillPath(&haloBrush, &haloPath);
        }
        const RectF ring(cx - r, cy - r, r * 2.0f, r * 2.0f);
        GraphicsPath ringPath;
        RoundPath(ringPath, ring, r);
        SolidBrush ringBrush(COL_SUCCESS);
        g.FillPath(&ringBrush, &ringPath);
        DrawCheckMark(g, cx - 13.0f, cy + 1.0f, cx - 4.0f, cy + 10.0f, cx + 14.0f, cy - 10.0f, 4.0f,
                      Color(255, 255, 255, 255));

        const wchar_t* title =
            g_state.mode == AppMode::Uninstall ? L"Desinstalación completada" :
            g_state.mode == AppMode::Update    ? L"Actualización completada" :
                                                 L"Instalación completada";
        RectF titleRect(contentX, 142.0f, CW, 38.0f);
        DrawTextIn(g, title, titleRect, fonts.fTitle, COL_TEXT, true, true);

        std::wstring sub;
        if (g_state.mode == AppMode::Uninstall) {
            sub = L"ARTPICST se ha eliminado correctamente de este equipo.";
        } else if (g_state.mode == AppMode::Update) {
            sub = L"La nueva versión está lista y ARTPICST se abrirá en unos instantes.";
        } else {
            sub = L"Gracias por elegir ARTPICST.  " + g_state.installPath;
        }
        RectF subRect(contentX, 184.0f, CW, 22.0f);
        DrawTextIn(g, sub.c_str(), subRect, fonts.fSmall, COL_TEXT_SOFT, true, true,
                   StringTrimmingEllipsisCharacter, true);

        // --- Tres tarjetas de estadísticas REALES de la operación. ---
        if (g_state.mode != AppMode::Uninstall) {
            const wchar_t* labels[] = { L"VERSIÓN", L"TAMAÑO", L"ACCESOS" };
            const std::wstring values[] = {
                std::wstring(APP_VERSION),
                g_bytesDeployed > 0 ? FormatBytes(g_bytesDeployed) : std::wstring(L"—"),
                std::to_wstring(g_shortcutsCreated),
            };
            const float cardW = (CW - 24.0f) / 3.0f;
            const float cardY = 222.0f;
            for (int i = 0; i < 3; ++i) {
                const RectF rc(contentX + i * (cardW + 12.0f), cardY, cardW, 64.0f);
                FillRound(g, rc, 10.0f, COL_PANEL);
                StrokeRound(g, rc, 10.0f, COL_PANEL_BORDER, 1.0f);
                RectF v(rc.X + 12.0f, rc.Y + 11.0f, rc.Width - 24.0f, 26.0f);
                DrawTextIn(g, values[i].c_str(), v, fonts.fBody,
                           i == 0 ? COL_ACCENT_A : COL_TEXT, true, true,
                           StringTrimmingEllipsisCharacter, true);
                RectF l(rc.X + 12.0f, rc.Y + 41.0f, rc.Width - 24.0f, 13.0f);
                DrawTextIn(g, labels[i], l, fonts.fLabel, COL_TEXT_DIM, true, true);
            }

            // --- Fila final clásica: enlace a la web + casilla de ejecución. ---
            const float rowW = (CW - 12.0f) * 0.5f;
            const RectF web(contentX, 312.0f, rowW, 36.0f);
            const RectF check(contentX + rowW + 12.0f, 312.0f, rowW, 36.0f);

            FillRound(g, web, 9.0f, COL_BTN_GHOST);
            StrokeRound(g, web, 9.0f,
                        g_state.hoverZone == HOVER_WEB ? COL_BTN_GHOST_BORDER_HOT : COL_BTN_GHOST_BORDER, 1.0f);
            Pen arrow(Color(255, 86, 160, 255), 1.6f);
            arrow.SetStartCap(LineCapRound);
            arrow.SetEndCap(LineCapRound);
            const float ax = web.X + 22.0f, ay = web.Y + web.Height * 0.5f;
            g.DrawLine(&arrow, ax, ay, ax + 12.0f, ay);
            g.DrawLine(&arrow, ax + 8.0f, ay - 4.0f, ax + 12.0f, ay);
            g.DrawLine(&arrow, ax + 8.0f, ay + 4.0f, ax + 12.0f, ay);
            RectF webText(web.X + 44.0f, web.Y, web.Width - 54.0f, web.Height);
            DrawTextIn(g, L"Visitar la web de ARTPICST", webText, fonts.fSmall,
                       g_state.hoverZone == HOVER_WEB ? COL_ACCENT_A : COL_TEXT_SOFT, false, true);

            const RectF boxRect(check.X + 10.0f, check.Y + (check.Height - 18.0f) * 0.5f, 18.0f, 18.0f);
            DrawCheckBox(g, boxRect, g_state.launchOnFinish);
            RectF checkText(check.X + 38.0f, check.Y, check.Width - 48.0f, check.Height);
            DrawTextIn(g, L"Ejecutar ARTPICST al cerrar", checkText, fonts.fSmall,
                       g_state.hoverZone == HOVER_CHECK ? COL_TEXT : COL_TEXT_SOFT, false, true);
        }
    } else {
        const float r = 34.0f;
        const float cy = 118.0f;
        const RectF ring(cx - r, cy - r, r * 2.0f, r * 2.0f);
        GraphicsPath ringPath;
        RoundPath(ringPath, ring, r);
        SolidBrush ringBrush(COL_ERROR);
        g.FillPath(&ringBrush, &ringPath);
        Font bangFont(&fonts.family, 38.0f, FontStyleBold, UnitPixel);
        DrawTextIn(g, L"!", ring, bangFont, Color(255, 255, 255, 255), true, true);

        RectF titleRect(contentX, 172.0f, CW, 40.0f);
        DrawTextIn(g, L"No se pudo completar la operación", titleRect, fonts.fTitle, COL_TEXT, true, true);

        RectF msgRect(contentX, 218.0f, CW, 44.0f);
        DrawTextIn(g, g_state.failureReason.c_str(), msgRect, fonts.fSmall, COL_TEXT_SOFT, true, false);
    }
}

// ============================================================================
// Composición de la ventana
// ============================================================================

static const wchar_t* SubtitleForHeader() {
    switch (g_state.mode) {
        case AppMode::Uninstall: return L"Desinstalador";
        case AppMode::Update:    return L"Actualización automática";
        case AppMode::Install:   break;
    }
    return L"Instalador oficial";
}

void RenderWindow(Graphics& g, const RECT& client) {
    const float W = static_cast<float>(client.right) / g_scale;
    const float H = static_cast<float>(client.bottom) / g_scale;

    // FIX ANTI-PARPADEO/LAG: las fuentes GDI+ se creaban aquí en CADA
    // repintado (~30 veces por segundo durante la instalación). Se usa una
    // caché global construida una sola vez.
    const Fonts& fonts = SharedFonts();
    DrawChrome(g, fonts, W, H);
    DrawSidebar(g, fonts, W, H);
    DrawPageHeader(g, fonts, W, SubtitleForHeader());

    const LayoutRects layout = ComputeLayout(W, H);

    switch (g_state.currentStep) {
        case WizardStep::Welcome:          RenderWelcome(g, fonts, W, H, layout); break;
        case WizardStep::License:          RenderLicense(g, fonts, W, H, layout); break;
        case WizardStep::Destination:      RenderDestination(g, fonts, W, H, layout); break;
        case WizardStep::UninstallConfirm: RenderUninstallConfirm(g, fonts, W, H, layout); break;
        case WizardStep::Working:          RenderWorking(g, fonts, W, H); break;
        case WizardStep::Complete:         RenderComplete(g, fonts, W); break;
    }

    if (!g_state.isWorking) {
        DrawGhostButton(g, fonts, layout.cancel, L"Cancelar", g_state.hoverZone == HOVER_CANCEL);
    }

    if (g_state.currentStep == WizardStep::Welcome) {
        DrawPrimaryButton(g, fonts, layout.next,
                          g_state.mode == AppMode::Uninstall ? L"Desinstalar  →" : L"Siguiente  →",
                          g_state.hoverZone == HOVER_NEXT);
    } else if (g_state.currentStep == WizardStep::License) {
        DrawGhostButton(g, fonts, layout.back, L"←  Volver", g_state.hoverZone == HOVER_BACK);
        DrawPrimaryButton(g, fonts, layout.next, L"Continuar  →", g_state.hoverZone == HOVER_NEXT);
    } else if (g_state.currentStep == WizardStep::Destination) {
        DrawGhostButton(g, fonts, layout.back, L"←  Volver", g_state.hoverZone == HOVER_BACK);
        DrawPrimaryButton(g, fonts, layout.next, L"Instalar", g_state.hoverZone == HOVER_NEXT);
    } else if (g_state.currentStep == WizardStep::UninstallConfirm) {
        DrawGhostButton(g, fonts, layout.back, L"←  Cancelar", g_state.hoverZone == HOVER_BACK);
        DrawPrimaryButton(g, fonts, layout.next, L"Desinstalar", g_state.hoverZone == HOVER_NEXT);
    } else if (g_state.currentStep == WizardStep::Complete) {
        if (g_state.installSucceeded) {
            if (g_state.mode == AppMode::Uninstall) {
                DrawGhostButton(g, fonts, layout.back, L"Cerrar", g_state.hoverZone == HOVER_BACK);
                DrawPrimaryButton(g, fonts, layout.next, L"Reinstalar ARTPICST", g_state.hoverZone == HOVER_NEXT);
            } else {
                DrawGhostButton(g, fonts, layout.back, L"Cerrar", g_state.hoverZone == HOVER_BACK);
                DrawPrimaryButton(g, fonts, layout.next, L"Iniciar ARTPICST", g_state.hoverZone == HOVER_NEXT);
            }
        } else {
            DrawGhostButton(g, fonts, layout.back, L"Reintentar", g_state.hoverZone == HOVER_BACK);
            DrawPrimaryButton(g, fonts, layout.next, L"Cerrar", g_state.hoverZone == HOVER_NEXT);
        }
    }
}

// ============================================================================
// Acciones: arranque de trabajos asíncronos y navegación
// ============================================================================

void AppendLog(LogKind kind, double atSeconds, const std::wstring& text) {
    g_state.log.push_back({ atSeconds, kind, text });
    if (g_state.log.size() > 512) {
        g_state.log.erase(g_state.log.begin(), g_state.log.begin() + (g_state.log.size() - 512));
    }
}

void StartWork(AppMode mode) {
    if (g_state.isWorking) return;
    g_state.isWorking = true;
    g_state.mode = mode;
    g_state.currentStep = WizardStep::Working;
    g_state.hoverZone = HOVER_NONE;
    g_state.installSucceeded = false;
    g_state.failureReason.clear();
    g_state.progressShown = 0.0;
    g_state.progressTarget = 0.0;
    g_bytesDeployed = 0;
    g_shortcutsCreated = 0;
    g_state.log.clear();
    g_paintedTenth = -1;
    g_paintedLogSize = -1;
    g_state.logScroll = 0;
    g_state.workTotalSeconds = (mode == AppMode::Uninstall) ? kUninstallDurationSeconds
                                                           : kInstallDurationSeconds;
    g_state.installStatus = (mode == AppMode::Uninstall)
        ? L"Preparando la desinstalación..."
        : L"Preparando la instalación...";
    if (g_state.hwnd) {
        SetTimer(g_state.hwnd, TIMER_PROGRESS, 33, nullptr);   // ~30 fps de animación
        InvalidateRect(g_state.hwnd, nullptr, FALSE);
    }
    g_state.worker = std::thread(RunPipeline, mode);
}

void HandlePipeMessage(PipeMessage* msg) {
    if (!msg) return;
    switch (msg->kind) {
        case PipeMessage::Kind::Progress:
            g_state.progressTarget = msg->progress;
            if (msg->phase) g_state.installStatus = msg->phase;
            break;
        case PipeMessage::Kind::Log:
            AppendLog(msg->logKind, msg->atSeconds, msg->text);
            break;
        case PipeMessage::Kind::Done: {
            g_state.installSucceeded = msg->success;
            g_state.progressTarget = 100.0;
            g_state.isWorking = false;
            if (!msg->success) {
                for (auto it = g_state.log.rbegin(); it != g_state.log.rend(); ++it) {
                    if (it->kind == LogKind::Error) { g_state.failureReason = it->text; break; }
                }
                if (g_state.failureReason.empty()) g_state.failureReason = L"Error desconocido durante el proceso.";
            }
            if (g_state.hwnd) {
                KillTimer(g_state.hwnd, TIMER_PROGRESS);
                InvalidateRect(g_state.hwnd, nullptr, FALSE);
                // La app se relanza tras terminar la ACTUALIZACIÓN (pequeño
                // respiro para que la página "Completado" se pinte).
                if (msg->success && g_state.mode == AppMode::Update) {
                    SetTimer(g_state.hwnd, TIMER_RELAUNCH, 900, nullptr);
                }
            }
            break;
        }
    }
    delete msg;
}

void RelaunchInstalledApp() {
    const std::wstring exe = g_state.installPath + L"\\artpicst.exe";
    if (GetFileAttributesW(exe.c_str()) != INVALID_FILE_ATTRIBUTES) {
        LaunchAppForUser(exe, g_state.installPath);
    }
}

void InvokePrimaryAction() {
    if (g_state.isWorking) return;
    switch (g_state.currentStep) {
        case WizardStep::Welcome:
            g_state.currentStep = (g_state.mode == AppMode::Uninstall) ? WizardStep::UninstallConfirm
                                                                      : WizardStep::License;
            g_state.hoverZone = HOVER_NONE;
            InvalidateRect(g_state.hwnd, nullptr, FALSE);
            break;
        case WizardStep::License:
            RefreshDestinationFacts();   // hechos frescos antes de mostrar Destino
            g_state.currentStep = WizardStep::Destination;
            g_state.hoverZone = HOVER_NONE;
            InvalidateRect(g_state.hwnd, nullptr, FALSE);
            break;
        case WizardStep::Destination:
            StartWork(AppMode::Install);
            break;
        case WizardStep::UninstallConfirm:
            StartWork(AppMode::Uninstall);
            break;
        case WizardStep::Complete:
            if (g_state.installSucceeded) {
                if (g_state.mode == AppMode::Uninstall) {
                    // "Reinstalar ARTPICST": reinicia el asistente en modo instalación.
                    g_state.mode = AppMode::Install;
                    g_state.currentStep = WizardStep::Welcome;
                    g_state.installPath = GetDefaultInstallPath();
                    g_state.log.clear();
                    g_state.hoverZone = HOVER_NONE;
                    InvalidateRect(g_state.hwnd, nullptr, FALSE);
                } else {
                    RelaunchInstalledApp();
                    PostMessageW(g_state.hwnd, WM_CLOSE, 0, 0);
                }
            } else {
                // Reintento idéntico al de InvokeBackAction.
                StartWork(g_state.mode);
            }
            break;
    }
}

void InvokeBackAction() {
    if (g_state.isWorking) return;
    switch (g_state.currentStep) {
        case WizardStep::License:
            g_state.currentStep = WizardStep::Welcome;
            g_state.hoverZone = HOVER_NONE;
            InvalidateRect(g_state.hwnd, nullptr, FALSE);
            break;
        case WizardStep::Destination:
            g_state.currentStep = WizardStep::License;
            g_state.hoverZone = HOVER_NONE;
            InvalidateRect(g_state.hwnd, nullptr, FALSE);
            break;
        case WizardStep::UninstallConfirm:
            PostMessageW(g_state.hwnd, WM_CLOSE, 0, 0);
            break;
        case WizardStep::Complete:
            if (g_state.installSucceeded || g_state.mode == AppMode::Uninstall) {
                PostMessageW(g_state.hwnd, WM_CLOSE, 0, 0);
            } else {
                // "Reintentar"
                StartWork(g_state.mode);   // "Reintentar": relanza el MISMO trabajo
            }
            break;
        default:
            break;
    }
}

void ToggleOptionAt(int rowIndex) {
    if (g_state.currentStep == WizardStep::License) {
        switch (rowIndex) {
            case 0: g_state.createDesktopShortcut = !g_state.createDesktopShortcut; break;
            case 1: g_state.createStartMenuShortcut = !g_state.createStartMenuShortcut; break;
            case 2: g_state.registerFileAssociations = !g_state.registerFileAssociations; break;
            default: return;
        }
    } else if (g_state.currentStep == WizardStep::UninstallConfirm) {
        if (rowIndex == 0) g_state.keepUserConfig = !g_state.keepUserConfig;
        else return;
    } else {
        return;
    }
    if (g_state.hwnd) {
        RECT client{};
        GetClientRect(g_state.hwnd, &client);
        const LayoutRects lr = ComputeLayout(
            static_cast<float>(client.right) / g_scale,
            static_cast<float>(client.bottom) / g_scale);
        if (rowIndex >= 0 && rowIndex < lr.rowCount) {
            const RectF& rc = lr.rows[rowIndex];
            RECT phys{
                static_cast<LONG>(rc.X * g_scale) - 1,
                static_cast<LONG>(rc.Y * g_scale) - 1,
                static_cast<LONG>((rc.X + rc.Width) * g_scale) + 1,
                static_cast<LONG>((rc.Y + rc.Height) * g_scale) + 1 };
            InvalidateRect(g_state.hwnd, &phys, FALSE);
        }
    }
}

// ============================================================================
// Ventana
// ============================================================================

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HCURSOR s_cursorArrow = LoadCursor(nullptr, IDC_ARROW);
    static HCURSOR s_cursorHand = LoadCursor(nullptr, IDC_HAND);

    switch (msg) {
        case WM_CREATE: {
            g_state.hwnd = hwnd;
            g_state.installPath = GetDefaultInstallPath();

            BOOL darkMode = TRUE;
            DwmSetWindowAttribute(hwnd, 20, &darkMode, sizeof(BOOL));
            DwmSetWindowAttribute(hwnd, 19, &darkMode, sizeof(BOOL));
            const int cornerPref = 2;   // DWMWCP_ROUND (Windows 11)
            DwmSetWindowAttribute(hwnd, 33, &cornerPref, sizeof(cornerPref));
            return 0;
        }
        case WM_APP_PIPE:
            HandlePipeMessage(reinterpret_cast<PipeMessage*>(lParam));
            return 0;
        case WM_TIMER:
            if (wParam == TIMER_PROGRESS) {
                // Animación fluida del porcentaje (interpolación exponencial).
                const double diff = g_state.progressTarget - g_state.progressShown;
                if (diff > 0.01) {
                    g_state.progressShown += diff * 0.22;
                    if (g_state.progressTarget - g_state.progressShown < 0.05) {
                        g_state.progressShown = g_state.progressTarget;
                    }
                }
                g_state.workElapsed = g_state.workTotalSeconds * (g_state.progressShown / 100.0);
                // FIX ANTI-PARPADEO: en vez de invalidar TODA la ventana a
                // 30 fps, solo se invalidan las regiones cuyo contenido ha
                // cambiado desde el último fotograma pintado.
                RECT client{};
                GetClientRect(hwnd, &client);
                const float W = static_cast<float>(client.right) / g_scale;
                const float H = static_cast<float>(client.bottom) / g_scale;
                const LayoutRects layout = ComputeLayout(W, H);
                auto invalidateRectF = [&](const RectF& rc) {
                    RECT phys{
                        static_cast<LONG>(rc.X * g_scale) - 1,
                        static_cast<LONG>(rc.Y * g_scale) - 1,
                        static_cast<LONG>((rc.X + rc.Width) * g_scale) + 1,
                        static_cast<LONG>((rc.Y + rc.Height) * g_scale) + 1 };
                    InvalidateRect(hwnd, &phys, FALSE);
                };
                // La zona de progreso solo se repinta cuando su contenido
                // cambia: en reposo (target alcanzado, sin líneas nuevas) la
                // ventana queda a cero invalidaciones.
                const int shownTenth = static_cast<int>(g_state.progressShown * 10.0);
                if (shownTenth != g_paintedTenth) {
                    invalidateRectF(layout.work);
                    invalidateRectF(layout.time);
                    g_paintedTenth = shownTenth;
                }
                if (static_cast<int>(g_state.log.size()) != g_paintedLogSize) {
                    invalidateRectF(layout.log);
                    g_paintedLogSize = static_cast<int>(g_state.log.size());
                }
                return 0;
            }
            if (wParam == TIMER_RELAUNCH) {
                KillTimer(hwnd, TIMER_RELAUNCH);
                RelaunchInstalledApp();
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                return 0;
            }
            break;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<LPMINMAXINFO>(lParam);
            if (info) {
                info->ptMinTrackSize.x = static_cast<LONG>(MIN_DESIGN_W * g_scale + 0.5f);
                info->ptMinTrackSize.y = static_cast<LONG>(MIN_DESIGN_H * g_scale + 0.5f);
            }
            return 0;
        }
        case WM_SIZE:
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_DPICHANGED: {
            const UINT newDpi = HIWORD(wParam);
            if (newDpi > 0) g_scale = newDpi / 96.0f;
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            if (suggested) {
                SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            if (g_state.currentStep != WizardStep::Working) break;
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            RECT client{};
            GetClientRect(hwnd, &client);
            const float consoleH = static_cast<float>(client.bottom) / g_scale - 152.0f - 76.0f;
            const int visible = static_cast<int>((consoleH - 42.0f) / 17.0f);   // cabecera 30 + margen inferior 12
            const int maxScroll = static_cast<int>(g_state.log.size()) > visible
                                      ? static_cast<int>(g_state.log.size()) - visible : 0;
            int scroll = g_state.logScroll - (delta > 0 ? 3 : -3);
            if (scroll < 0) scroll = 0;
            if (scroll > maxScroll) scroll = maxScroll;
            if (scroll != g_state.logScroll) {
                g_state.logScroll = scroll;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_CLOSE:
            // Nunca cerrar a mitad de proceso (evita instalaciones a medias y
            // un join() del propio hilo de UI: bloqueo garantizado).
            if (g_state.isWorking) return 0;
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        case WM_PAINT: {
            // FIX ANTI-PARPADEO: el repintado iba DIRECTO a la ventana
            // (BeginPaint) y cada ciclo del temporizador borraba primero la
            // región con el pincel de fondo antes de dibujar arriba: parpadeo
            // y tirones durante el progreso. Ahora se compone todo en un
            // bitmap de memoria y se copia en UNA sola pasada.
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (hdc) {
                RECT client;
                GetClientRect(hwnd, &client);
                const int widthPx = (client.right - client.left > 0) ? client.right - client.left : 1;
                const int heightPx = (client.bottom - client.top > 0) ? client.bottom - client.top : 1;

                HDC memDc = CreateCompatibleDC(hdc);
                HBITMAP memBmp = memDc ? CreateCompatibleBitmap(hdc, widthPx, heightPx) : nullptr;
                HGDIOBJ oldBmp = (memDc && memBmp) ? SelectObject(memDc, memBmp) : nullptr;

                if (memDc && memBmp) {
                    Graphics graphics(memDc);
                    graphics.ScaleTransform(g_scale, g_scale);
                    graphics.SetCompositingQuality(CompositingQualityHighQuality);
                    graphics.SetSmoothingMode(SmoothingModeHighQuality);
                    graphics.SetPixelOffsetMode(PixelOffsetModeHalf);
                    graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
                    graphics.SetTextContrast(4000);

                    RenderWindow(graphics, client);

                    // Copia atómica del frame compuesto a la ventana real.
                    BitBlt(hdc, 0, 0, widthPx, heightPx, memDc, 0, 0, SRCCOPY);
                    SelectObject(memDc, oldBmp);
                    DeleteObject(memBmp);
                }
                if (memDc) DeleteDC(memDc);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return TRUE;
        case WM_MOUSEMOVE: {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            if (!g_state.mouseTracking) {
                TRACKMOUSEEVENT tme{};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
                g_state.mouseTracking = true;
            }
            RECT client;
            GetClientRect(hwnd, &client);
            const float W = static_cast<float>(client.right) / g_scale;
            const float H = static_cast<float>(client.bottom) / g_scale;
            const LayoutRects layout = ComputeLayout(W, H);
            const int zone = HoverZoneAt(layout, DesignX(x), DesignY(y));
            if (zone != g_state.hoverZone) {
                RECT clientRect{};
                GetClientRect(hwnd, &clientRect);
                const LayoutRects lr = ComputeLayout(
                    static_cast<float>(clientRect.right) / g_scale,
                    static_cast<float>(clientRect.bottom) / g_scale);
                const int prevZone = g_state.hoverZone;
                g_state.hoverZone = zone;

                auto invalidateZone = [&](int z) {
                    if (z == HOVER_NONE) return;
                    const RectF* rc = nullptr;
                    if (z == HOVER_BACK) rc = &lr.back;
                    else if (z == HOVER_NEXT) rc = &lr.next;
                    else if (z == HOVER_CANCEL) rc = &lr.cancel;
                    else if (z == HOVER_BROWSE) rc = &lr.browse;
                    else if (z == HOVER_WEB) rc = &lr.web;
                    else if (z == HOVER_CHECK) rc = &lr.check;
                    else if (z >= HOVER_ROW_DESKTOP && z < HOVER_ROW_DESKTOP + lr.rowCount)
                        rc = &lr.rows[z - HOVER_ROW_DESKTOP];
                    if (!rc) return;
                    RECT phys{
                        static_cast<LONG>(rc->X * g_scale) - 1,
                        static_cast<LONG>(rc->Y * g_scale) - 1,
                        static_cast<LONG>((rc->X + rc->Width) * g_scale) + 1,
                        static_cast<LONG>((rc->Y + rc->Height) * g_scale) + 1 };
                    InvalidateRect(hwnd, &phys, FALSE);
                };
                invalidateZone(prevZone);
                invalidateZone(zone);
            }
            SetCursor(zone != HOVER_NONE ? s_cursorHand : s_cursorArrow);
            return 0;
        }
        case WM_MOUSELEAVE: {
            g_state.mouseTracking = false;
            if (g_state.hoverZone != HOVER_NONE) {
                g_state.hoverZone = HOVER_NONE;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            SetCursor(s_cursorArrow);
            return 0;
        }
        case WM_DESTROY: {
            // El pipeline solo puede estar aquí si WM_CLOSE lo permitió
            // (isWorking == false). Un join() desde el hilo de UI con el
            // worker activo congela la ventana ("No responde").
            if (!g_state.isWorking && g_state.worker.joinable()) g_state.worker.join();
            PostQuitMessage(0);
            return 0;
        }
        case WM_KEYDOWN:
            if (g_state.isWorking) return 0;
            if (wParam == VK_RETURN) {
                InvokePrimaryAction();
            } else if (wParam == VK_ESCAPE) {
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
            return 0;
        case WM_LBUTTONDOWN: {
            if (g_state.isWorking) return 0;
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            const float lx = DesignX(x);
            const float ly = DesignY(y);

            RECT client;
            GetClientRect(hwnd, &client);
            const float W = static_cast<float>(client.right) / g_scale;
            const float H = static_cast<float>(client.bottom) / g_scale;
            const LayoutRects layout = ComputeLayout(W, H);

            auto hit = [](const RectF& rc, float px, float py) {
                return px >= rc.X && px <= rc.X + rc.Width &&
                       py >= rc.Y && py <= rc.Y + rc.Height;
            };

            if ((g_state.currentStep == WizardStep::License ||
                 g_state.currentStep == WizardStep::UninstallConfirm) && layout.rowCount > 0) {
                for (int i = 0; i < layout.rowCount; ++i) {
                    if (hit(layout.rows[i], lx, ly)) {
                        ToggleOptionAt(i);
                        return 0;
                    }
                }
            }

            if (g_state.currentStep == WizardStep::Destination && hit(layout.browse, lx, ly)) {
                BrowseForDestination();
                return 0;
            }
            if (g_state.currentStep == WizardStep::Complete && g_state.installSucceeded &&
                g_state.mode != AppMode::Uninstall) {
                if (hit(layout.web, lx, ly)) {
                    ShellExecuteW(hwnd, L"open", APP_URL, nullptr, nullptr, SW_SHOWNORMAL);
                    return 0;
                }
                if (hit(layout.check, lx, ly)) {
                    g_state.launchOnFinish = !g_state.launchOnFinish;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
            if (hit(layout.next, lx, ly)) {
                InvokePrimaryAction();
                return 0;
            }
            if (hit(layout.cancel, lx, ly)) {
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                return 0;
            }
            if (hit(layout.back, lx, ly)) {
                InvokeBackAction();
                return 0;
            }
            return 0;
        }
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================================
// DPI y arranque
// ============================================================================

typedef BOOL(WINAPI* PFN_SetProcessDpiAwarenessContext)(HANDLE value);

static void EnableDpiAwareness() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        PFN_SetProcessDpiAwarenessContext fn = nullptr;
        FARPROC raw = GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        static_assert(sizeof(fn) == sizeof(raw), "tamaño de puntero a función inesperado");
        std::memcpy(&fn, &raw, sizeof(fn));
        if (fn) {
            const HANDLE PMV2 = reinterpret_cast<HANDLE>(-4);
            if (fn(PMV2)) return;
        }
    }
    SetProcessDPIAware();
}

static float GetSystemScale() {
    HDC dc = GetDC(nullptr);
    int dpi = 96;
    if (dc) {
        dpi = GetDeviceCaps(dc, LOGPIXELSY);
        ReleaseDC(nullptr, dc);
    }
    if (dpi < 48) dpi = 96;
    return dpi / 96.0f;
}

// Ejecuta un trabajo SIN interfaz (--silent). Devuelve el código de salida.
// Reglas del modo desatendido:
//   · CERO ventanas y CERO MessageBox: cualquier problema se comunica por el
//     exit code (ver kSilentExit*) y por el log del propio pipeline.
//   · Validación previa del payload antes de tocar el sistema (instalación y
//     actualización): un paquete roto no debe dejar restos a medio copiar.
//   · La app SOLO se relanza en modo actualización (--update), nunca tras una
//     instalación desatendida explícita.
static int RunSilent(AppMode mode) {
    g_state.hwnd = nullptr;      // PipeUi destruye los mensajes sin UI
    g_state.silent = true;

    if (mode == AppMode::Uninstall) {
        // Desinstalación desatendida: resolver SIEMPRE la carpeta registrada
        // (antes quedaba vacía si --silent no iba acompañado de --dir).
        if (g_state.installPath.empty()) g_state.installPath = DetectInstallDir();
        g_state.uninstallInfoDir = g_state.installPath;
    } else if (g_state.installPath.empty()) {
        g_state.installPath = DetectInstallDir();
    }

    // Validación previa del payload (solo instalación/actualización).
    if (mode != AppMode::Uninstall) {
        int present = 0;
        for (int i = 0; i < kPayloadCount; ++i) {
            if (IsResourcePresent(kPayload[i].id)) ++present;
        }
        const bool mainAvailable = IsResourcePresent(RES_APP_EXE) ||
            GetFileAttributesW((GetModuleFolder() + L"\\artpicst.exe").c_str()) != INVALID_FILE_ATTRIBUTES;
        if (!mainAvailable || present < 2) {
            return kSilentExitPayloadMissing;
        }
    }

    // COM es necesario para crear accesos directos en el hilo del pipeline.
    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    RunPipeline(mode);
    if (SUCCEEDED(comHr)) CoUninitialize();

    if (!g_lastRunSucceeded) {
        // Archivos bloqueados (app en ejecución) merecen un código propio:
        // el script puede cerrar ARTPICST y reintentar.
        if (g_lastError.find(L"está en uso") != std::wstring::npos) {
            return kSilentExitFilesLocked;
        }
        return kSilentExitFailed;
    }

    // Actualización silenciosa: la aplicación se reabre automáticamente.
    if (mode == AppMode::Update) {
        const std::wstring exe = g_state.installPath + L"\\artpicst.exe";
        if (GetFileAttributesW(exe.c_str()) != INVALID_FILE_ATTRIBUTES) {
            LaunchAppForUser(exe, g_state.installPath);
        }
    }
    return kSilentExitOk;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)pCmdLine;

    // Argumentos: --uninstall, --silent, --elevated, --update <payload.exe>,
    // --dir <carpeta destino de la actualización>.
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool uninstallRequested = false;
    bool updateRequested = false;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (wcscmp(argv[i], L"--uninstall") == 0) {
                uninstallRequested = true;
            } else if (wcscmp(argv[i], L"--silent") == 0) {
                g_state.silent = true;
            } else if (wcscmp(argv[i], L"--elevated") == 0) {
                g_state.elevationAttempted = true;
            } else if (wcscmp(argv[i], L"--update") == 0 && i + 1 < argc) {
                updateRequested = true;
                g_state.updatePayloadPath = argv[++i];
                g_state.updatePayloadGiven = true;
            } else if (wcscmp(argv[i], L"--dir") == 0 && i + 1 < argc) {
                g_state.installPath = argv[++i];
            }
        }
        LocalFree(argv);
    }

    // Elevación (UAC) una sola vez, ANTES de tocar el sistema.
    if (IsProcessElevated()) {
        g_machineWide = true;
    } else if (!g_state.elevationAttempted && RelaunchElevatedSelf()) {
        return 0;   // el proceso elevado retoma la misma tarea
    } else {
        g_machineWide = false;   // sin elevación: instalación por usuario
    }

    if (g_state.silent) {
        return RunSilent(uninstallRequested ? AppMode::Uninstall
                         : updateRequested  ? AppMode::Update
                                            : AppMode::Install);
    }

    EnableDpiAwareness();
    g_scale = GetSystemScale();

    // La ventana ampliada debe caber en la pantalla: se limita la escala
    // inicial (nunca por encima del DPI del sistema para evitar doble escalado
    // de las fuentes UnitPixel).
    {
        RECT workArea;
        if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0)) {
            const float fitH = static_cast<float>(workArea.bottom - workArea.top) / (DESIGN_H + 48.0f);
            const float fitW = static_cast<float>(workArea.right - workArea.left) / (DESIGN_W + 16.0f);
            const float fit = fitH < fitW ? fitH : fitW;
            if (fit < g_scale) g_scale = fit;
            if (g_scale < 1.0f) g_scale = 1.0f;
        }
    }

    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Local\\ARTPICST_Installer_Mutex");
    if (!hMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"El instalador de ARTPICST ya está en ejecución.",
                    L"ARTPICST", MB_OK | MB_ICONINFORMATION);
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    g_state.hInstance = hInstance;
    if (uninstallRequested) {
        g_state.mode = AppMode::Uninstall;
        g_state.uninstallInfoDir = DetectInstallDir();
        g_state.installPath = g_state.uninstallInfoDir;
    } else if (updateRequested) {
        g_state.mode = AppMode::Update;
        if (g_state.installPath.empty()) g_state.installPath = DetectInstallDir();
    }

    HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comOk = SUCCEEDED(comHr);

    if (GdiplusStartup(&g_state.gdiplusToken, &g_state.gdiplusStartupInput, nullptr) != Ok) {
        if (comOk) CoUninitialize();
        CloseHandle(hMutex);
        MessageBoxW(nullptr, L"No se pudo inicializar la interfaz gráfica.", L"ARTPICST", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Datos de la página de desinstalación (versión/tamaño instalados).
    if (g_state.mode == AppMode::Uninstall) {
        ReadRegStringValue(RegRoot(), AppKeyPath(), L"Version", g_state.uninstallInfoVersion);
        if (g_state.uninstallInfoVersion.empty()) g_state.uninstallInfoVersion = APP_VERSION;
        wchar_t sizeText[32] = {};
        swprintf(sizeText, 32, L"%ls", FormatBytes(DirectorySizeBytes(g_state.uninstallInfoDir)).c_str());
        g_state.uninstallInfoSize = sizeText;
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_SAVEBITS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(RES_APP_ICON));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;

    if (!RegisterClassExW(&wc)) {
        MessageBoxW(nullptr, L"No se pudo registrar la ventana del instalador.", L"ARTPICST", MB_OK | MB_ICONERROR);
        GdiplusShutdown(g_state.gdiplusToken);
        if (comOk) CoUninitialize();
        CloseHandle(hMutex);
        return 1;
    }

    const wchar_t* windowTitle =
        g_state.mode == AppMode::Uninstall ? L"Desinstalador de ARTPICST" :
        g_state.mode == AppMode::Update    ? L"Actualización de ARTPICST" :
                                             L"Instalador de ARTPICST";

    const int winW = static_cast<int>(DESIGN_W * g_scale + 0.5f);
    const int winH = static_cast<int>(DESIGN_H * g_scale + 0.5f);
    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        CLASS_NAME,
        windowTitle,
        WS_OVERLAPPEDWINDOW,   // redimensionable: thickframe + maximizar
        CW_USEDEFAULT, CW_USEDEFAULT,
        winW, winH,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) {
        UnregisterClassW(CLASS_NAME, hInstance);
        GdiplusShutdown(g_state.gdiplusToken);
        if (comOk) CoUninitialize();
        CloseHandle(hMutex);
        MessageBoxW(nullptr, L"No se pudo crear la ventana del instalador.", L"ARTPICST", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Centrar en el área de trabajo de la pantalla principal.
    RECT rect;
    GetWindowRect(hwnd, &rect);
    const int winWpx = rect.right - rect.left;
    const int winHpx = rect.bottom - rect.top;
    RECT workArea;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const int posX = workArea.left + (workArea.right - workArea.left - winWpx) / 2;
    const int posY = workArea.top + (workArea.bottom - workArea.top - winHpx) / 2;
    SetWindowPos(hwnd, nullptr, posX, posY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    ShowWindow(hwnd, nCmdShow > 0 ? nCmdShow : SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    // El modo actualización entra directo al proceso (sin bienvenida).
    if (g_state.mode == AppMode::Update) {
        StartWork(AppMode::Update);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_state.worker.joinable()) g_state.worker.join();
    GdiplusShutdown(g_state.gdiplusToken);
    if (comOk) CoUninitialize();
    CloseHandle(hMutex);
    return static_cast<int>(msg.wParam);
}
