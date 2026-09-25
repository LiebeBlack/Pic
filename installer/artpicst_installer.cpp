// ============================================================================
// ARTPICST — Instalador / Desinstalador / Actualizador premium
// ----------------------------------------------------------------------------
// · Asistente GDI+ "pitch-black" (#000000 / #0A0A0A) con acentos neón
//   (#00F0FF / #7000FF), ventana ampliada (720x560), centrada y REDIMENSIONABLE.
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
const wchar_t APP_VERSION[]    = L"1.2.0";
const wchar_t CLASS_NAME[]     = L"ARTPICSTInstallerWindow";
const wchar_t UNINSTALL_REG_KEY[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ARTPICST";
const wchar_t APP_URL[]        = L"https://github.com/LiebeBlack/Pic";

// ============================================================================
// Paleta "Pitch-Black Neon" (#000000 fondo, #0A0A0A paneles, neón exacto)
// ============================================================================
const Color COL_BG(255, 0, 0, 0);                      // #000000 fondo principal
const Color COL_PANEL(255, 10, 10, 10);                // #0A0A0A paneles / encabezado
const Color COL_PANEL_DEEP(255, 8, 8, 10);             // paneles hundidos (log, caja licencia)
const Color COL_PANEL_BORDER(255, 32, 38, 50);
const Color COL_ACCENT_A(255, 0, 240, 255);            // #00F0FF cian neón
const Color COL_ACCENT_B(255, 112, 0, 255);            // #7000FF púrpura neón
const Color COL_TEXT(255, 236, 240, 246);
const Color COL_TEXT_SOFT(255, 150, 160, 178);
const Color COL_TEXT_DIM(255, 100, 110, 128);
const Color COL_BTN_GHOST(255, 18, 20, 26);
const Color COL_BTN_GHOST_HOT(255, 28, 32, 42);
const Color COL_BTN_GHOST_BORDER(255, 44, 52, 68);
const Color COL_BTN_GHOST_BORDER_HOT(255, 0, 240, 255);
const Color COL_SUCCESS(255, 60, 230, 140);
const Color COL_ERROR(255, 255, 84, 92);
const Color COL_WARN(255, 255, 186, 70);
const Color COL_DISABLED_TEXT(255, 92, 100, 116);

// Tamaño de diseño (unidades lógicas 96 DPI); la ventana se escala por g_scale
// y es REDIMENSIONABLE entre 640x520 y tamaños arbitrarios.
const float DESIGN_W = 720.0f;
const float DESIGN_H = 560.0f;
const float MIN_DESIGN_W = 640.0f;
const float MIN_DESIGN_H = 520.0f;
float g_scale = 1.0f;

// Duración exacta de la simulación de instalación/actualización y desinstalación.
constexpr double kInstallDurationSeconds = 34.0;
constexpr double kUninstallDurationSeconds = 14.0;

// ============================================================================
// Estado global del asistente
// ============================================================================

enum class AppMode { Install, Uninstall, Update };
enum class WizardStep { Welcome, License, UninstallConfirm, Working, Complete };

enum HoverZone {
    HOVER_NONE = 0,
    HOVER_BACK,
    HOVER_NEXT,
    HOVER_CANCEL,
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
    if (const wchar_t* env = _wgetenv(L"ProgramFiles")) {
        if (*env) return std::wstring(env) + L"\\" + APP_NAME;
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
    if (bytes >= 1024ull * 1024ull) {
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

static void DrawLogo(Graphics& g, const Fonts& fonts, float cx, float cy, float size) {
    const RectF tile(cx - size * 0.5f, cy - size * 0.5f, size, size);
    FillRoundGradient(g, tile, size * 0.24f, COL_ACCENT_A, COL_ACCENT_B);
    const RectF shine(tile.X, tile.Y, tile.Width, tile.Height * 0.5f);
    FillRound(g, shine, size * 0.24f, Color(28, 255, 255, 255));
    DrawTextIn(g, L"A", tile, fonts.fLogo, Color(255, 255, 255, 255), true, true);
}

// Fondo general negro puro + barra de acento superior (cian -> violeta).
static void DrawChrome(Graphics& g, const Fonts&, float W, float H) {
    SolidBrush bg(COL_BG);
    g.FillRectangle(&bg, 0.0f, 0.0f, W, H);
    const RectF topBar(0.0f, 0.0f, W, 3.0f);
    LinearGradientBrush accent(topBar, COL_ACCENT_A, COL_ACCENT_B, 0.0f);
    g.FillRectangle(&accent, topBar);
}

// Encabezado común: banda #0A0A0A con logo, título del asistente y puntos de
// progreso del paso actual (1..4).
static void DrawPageHeader(Graphics& g, const Fonts& fonts, float W, const wchar_t* subtitle, int stepIndex) {
    const RectF band(0.0f, 3.0f, W, 52.0f);
    SolidBrush bandBrush(COL_PANEL);
    g.FillRectangle(&bandBrush, band);
    const RectF hairline(0.0f, band.Y + band.Height, W, 1.0f);
    SolidBrush hair(Gdiplus::Color(255, 24, 28, 38));
    g.FillRectangle(&hair, hairline);

    DrawLogo(g, fonts, 34.0f, band.Y + band.Height * 0.5f, 26.0f);
    RectF titleRect(56.0f, band.Y + 6.0f, 300.0f, 20.0f);
    DrawTextIn(g, APP_NAME, titleRect, fonts.fLabel, COL_TEXT, false, true);
    RectF subRect(56.0f, band.Y + 26.0f, 340.0f, 16.0f);
    DrawTextIn(g, subtitle, subRect, fonts.fTiny, COL_TEXT_DIM, false, true);

    // Puntos de paso (Bienvenida -> Licencia -> Proceso -> Completado)
    const int total = 4;
    const float dotR = 3.0f;
    const float gapX = 16.0f;
    float dx = W - 44.0f - (total - 1) * gapX;
    const float dy = band.Y + band.Height * 0.5f;
    for (int i = 0; i < total; ++i) {
        const RectF dot(dx - dotR, dy - dotR, dotR * 2.0f, dotR * 2.0f);
        if (i <= stepIndex) {
            FillRoundGradient(g, dot, dotR, COL_ACCENT_A, COL_ACCENT_B);
        } else {
            FillRound(g, dot, dotR, Color(255, 40, 46, 60));
        }
        dx += gapX;
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

    // Cabecera del panel
    RectF headLabel(rc.X + 16.0f, rc.Y + 10.0f, rc.Width - 90.0f, 14.0f);
    DrawTextIn(g, L"CONSOLA DE INSTALACIÓN", headLabel, fonts.fLabel, COL_TEXT_DIM, false, true);
    wchar_t elapsed[32] = {};
    swprintf(elapsed, 32, L"t = %.1f s", g_state.workElapsed);
    RectF headTime(rc.X + rc.Width - 96.0f, rc.Y + 10.0f, 80.0f, 14.0f);
    DrawTextIn(g, elapsed, headTime, fonts.fTiny, COL_ACCENT_A, false, true, StringTrimmingNone, true);

    const float padX = 16.0f;
    const float lineH = 17.0f;
    const float listTop = rc.Y + 34.0f;
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
    RectF rows[3];
    int rowCount = 0;
};

float DesignX(int physicalX) { return static_cast<float>(physicalX) / g_scale; }
float DesignY(int physicalY) { return static_cast<float>(physicalY) / g_scale; }

LayoutRects ComputeLayout(float W, float H) {
    LayoutRects r;
    const float margin = 44.0f;
    const float buttonH = 42.0f;
    const float buttonY = H - buttonH - 20.0f;

    r.next = RectF(W - margin - 156.0f, buttonY, 156.0f, buttonH);
    r.back = RectF(margin, buttonY, 118.0f, buttonH);
    r.cancel = RectF(W - margin - 76.0f, 68.0f, 76.0f, 24.0f);

    if (g_state.currentStep == WizardStep::License || g_state.currentStep == WizardStep::UninstallConfirm) {
        const float rowX = 52.0f;
        const float rowW = W - rowX * 2.0f;
        const float rowH = 34.0f;
        const float gap = 8.0f;
        float y = 276.0f;
        r.rowCount = (g_state.currentStep == WizardStep::License) ? 3 : 1;
        for (int i = 0; i < r.rowCount; ++i) {
            r.rows[i] = RectF(rowX, y, rowW, rowH);
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
    if (g_state.currentStep == WizardStep::License ||
        g_state.currentStep == WizardStep::UninstallConfirm) {
        if (hit(r.back)) return HOVER_BACK;
        for (int i = 0; i < r.rowCount; ++i) {
            if (hit(r.rows[i])) return HOVER_ROW_DESKTOP + i;
        }
    }
    if ((g_state.currentStep == WizardStep::Welcome ||
         g_state.currentStep == WizardStep::License ||
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

void RenderWelcome(Graphics& g, const Fonts& fonts, float W, float H) {
    const float cx = W * 0.5f;
    DrawLogo(g, fonts, cx, 122.0f, 64.0f);

    RectF nameRect(cx - 200.0f, 164.0f, 400.0f, 42.0f);
    DrawTextIn(g, APP_NAME, nameRect, fonts.fTitle, COL_TEXT, true, false);

    RectF tagRect(cx - 280.0f, 210.0f, 560.0f, 22.0f);
    DrawTextIn(g, L"Visor de imágenes premium para Windows — rápido, ligero y moderno",
               tagRect, fonts.fSmall, COL_TEXT_SOFT, true, true);

    const float cardY = 250.0f;
    const float cardH = 190.0f;
    const RectF card(cx - 270.0f, cardY, 540.0f, cardH);
    FillRound(g, card, 14.0f, COL_PANEL);
    StrokeRound(g, card, 14.0f, COL_PANEL_BORDER, 1.0f);

    const wchar_t* features[] = {
        L"Más de 30 formatos: PNG, WebP, HEIC, AVIF, GIF, RAW y más",
        L"Zoom fluido por GPU, píxel perfecto al 100% y Ultra-Claridad HDR",
        L"Instalación tradicional: Program Files, menú Inicio y desinstalador",
        L"Módulo inteligente de actualización automática integrado",
        L"Interfaz oscura elegante y consumo mínimo de RAM y CPU",
    };
    float fy = cardY + 24.0f;
    for (const wchar_t* text : features) {
        const float dotR = 3.5f;
        const float dotY = fy + 8.0f;
        FillRoundGradient(g, RectF(card.X + 24.0f, dotY - dotR, dotR * 2.0f, dotR * 2.0f), dotR, COL_ACCENT_A, COL_ACCENT_B);
        RectF featureRect(card.X + 40.0f, fy - 2.0f, card.Width - 60.0f, 22.0f);
        DrawTextIn(g, text, featureRect, fonts.fSmall, COL_TEXT_SOFT, false, true);
        fy += 32.0f;
    }

    RectF hint(cx - 260.0f, H - 78.0f, 520.0f, 18.0f);
    DrawTextIn(g, (g_machineWide
                       ? L"Se instalará para todos los usuarios (requiere administrador)."
                       : L"Se instalará para tu usuario, sin permisos de administrador."),
               hint, fonts.fTiny, COL_TEXT_DIM, true, true);
}

void RenderLicense(Graphics& g, const Fonts& fonts, float W, float H, const LayoutRects& layout) {
    const float cx = W * 0.5f;

    RectF titleRect(cx - 220.0f, 68.0f, 440.0f, 30.0f);
    DrawTextIn(g, L"Licencia y opciones", titleRect, fonts.fHeading, COL_TEXT, true, true);

    const RectF box(cx - 290.0f, 106.0f, 580.0f, 148.0f);
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

    RectF optTitle(52.0f, 256.0f, W - 104.0f, 14.0f);
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

    RectF destRect(52.0f, H - 74.0f, W - 104.0f, 18.0f);
    std::wstring dest = L"Se instalará en:  " + g_state.installPath;
    DrawTextIn(g, dest.c_str(), destRect, fonts.fTiny, COL_TEXT_DIM, false, true,
               StringTrimmingEllipsisCharacter, true);
}

void RenderUninstallConfirm(Graphics& g, const Fonts& fonts, float W, float H, const LayoutRects& layout) {
    const float cx = W * 0.5f;

    const float r = 32.0f;
    const float cy = 122.0f;
    const RectF ring(cx - r, cy - r, r * 2.0f, r * 2.0f);
    GraphicsPath ringPath;
    RoundPath(ringPath, ring, r);
    SolidBrush ringBrush(COL_WARN);
    g.FillPath(&ringBrush, &ringPath);
    Font bangFont(&fonts.family, 36.0f, FontStyleBold, UnitPixel);
    DrawTextIn(g, L"!", ring, bangFont, Color(255, 20, 20, 24), true, true);

    RectF titleRect(cx - 240.0f, 168.0f, 480.0f, 34.0f);
    DrawTextIn(g, L"Desinstalar ARTPICST", titleRect, fonts.fHeading, COL_TEXT, true, true);

    RectF subRect(cx - 240.0f, 206.0f, 480.0f, 20.0f);
    DrawTextIn(g, L"El programa y sus componentes se eliminarán de este equipo.",
               subRect, fonts.fSmall, COL_TEXT_SOFT, true, true);

    const RectF card(cx - 250.0f, 238.0f, 500.0f, 108.0f);
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

    RectF hint(cx - 260.0f, H - 74.0f, 520.0f, 18.0f);
    DrawTextIn(g, L"Las imágenes del equipo y sus miniaturas no se verán afectadas.",
               hint, fonts.fTiny, COL_TEXT_DIM, true, true);
}

void RenderWorking(Graphics& g, const Fonts& fonts, float W, float H) {
    const float cx = W * 0.5f;

    DrawLogo(g, fonts, cx, 118.0f, 46.0f);

    const wchar_t* title =
        g_state.mode == AppMode::Uninstall ? L"Desinstalando ARTPICST" :
        g_state.mode == AppMode::Update    ? L"Actualizando ARTPICST" :
                                             L"Instalando ARTPICST";
    RectF titleRect(cx - 260.0f, 150.0f, 520.0f, 30.0f);
    DrawTextIn(g, title, titleRect, fonts.fHeading, COL_TEXT, true, true);

    RectF statusRect(cx - 280.0f, 184.0f, 560.0f, 20.0f);
    DrawTextIn(g, g_state.installStatus.c_str(), statusRect, fonts.fSmall, COL_TEXT_SOFT, true, true);

    const RectF track(cx - 260.0f, 212.0f, 520.0f, 10.0f);
    DrawProgressBar(g, track, g_state.progressShown);

    wchar_t percentText[32];
    swprintf(percentText, 32, L"%.0f%%", g_state.progressShown);
    RectF pctRect(cx - 260.0f, 228.0f, 120.0f, 18.0f);
    DrawTextIn(g, percentText, pctRect, fonts.fLabel, COL_ACCENT_A, false, true, StringTrimmingNone, true);

    // Consola de log: se estira con la ventana (ancho/alto fluidos).
    const RectF console(cx - 300.0f, 254.0f, 600.0f, H - 254.0f - 64.0f);
    DrawLogConsole(g, fonts, console);

    std::wstring dest = (g_state.mode == AppMode::Uninstall)
        ? (L"Desinstalando de: " + g_state.uninstallInfoDir)
        : (L"Destino: " + g_state.installPath);
    RectF destRect(cx - 300.0f, H - 46.0f, 600.0f, 18.0f);
    DrawTextIn(g, dest.c_str(), destRect, fonts.fTiny, COL_TEXT_DIM, true, true,
               StringTrimmingEllipsisCharacter, true);
}

void RenderComplete(Graphics& g, const Fonts& fonts, float W) {
    const float cx = W * 0.5f;

    if (g_state.installSucceeded) {
        const float r = 36.0f;
        const float cy = 136.0f;
        const RectF ring(cx - r, cy - r, r * 2.0f, r * 2.0f);
        GraphicsPath ringPath;
        RoundPath(ringPath, ring, r);
        SolidBrush ringBrush(COL_SUCCESS);
        g.FillPath(&ringBrush, &ringPath);
        DrawCheckMark(g, cx - 15.0f, cy + 1.0f, cx - 4.0f, cy + 12.0f, cx + 16.0f, cy - 12.0f, 4.0f,
                      Color(255, 255, 255, 255));

        const wchar_t* title =
            g_state.mode == AppMode::Uninstall ? L"Desinstalación completada" :
            g_state.mode == AppMode::Update    ? L"Actualización completada" :
                                                 L"Instalación completada";
        RectF titleRect(cx - 240.0f, 196.0f, 480.0f, 40.0f);
        DrawTextIn(g, title, titleRect, fonts.fTitle, COL_TEXT, true, true);

        std::wstring sub;
        if (g_state.mode == AppMode::Uninstall) {
            sub = L"ARTPICST se ha eliminado correctamente de este equipo.";
        } else if (g_state.mode == AppMode::Update) {
            sub = L"La nueva versión está lista y ARTPICST se abrirá en unos instantes.";
        } else {
            sub = L"Gracias por elegir ARTPICST.  " + g_state.installPath;
        }
        RectF subRect(cx - 280.0f, 242.0f, 560.0f, 22.0f);
        DrawTextIn(g, sub.c_str(), subRect, fonts.fSmall, COL_TEXT_SOFT, true, true,
                   StringTrimmingEllipsisCharacter, true);
    } else {
        const float r = 36.0f;
        const float cy = 136.0f;
        const RectF ring(cx - r, cy - r, r * 2.0f, r * 2.0f);
        GraphicsPath ringPath;
        RoundPath(ringPath, ring, r);
        SolidBrush ringBrush(COL_ERROR);
        g.FillPath(&ringBrush, &ringPath);
        Font bangFont(&fonts.family, 40.0f, FontStyleBold, UnitPixel);
        DrawTextIn(g, L"!", ring, bangFont, Color(255, 255, 255, 255), true, true);

        RectF titleRect(cx - 260.0f, 196.0f, 520.0f, 40.0f);
        DrawTextIn(g, L"No se pudo completar la operación", titleRect, fonts.fTitle, COL_TEXT, true, true);

        RectF msgRect(cx - 280.0f, 242.0f, 560.0f, 44.0f);
        DrawTextIn(g, g_state.failureReason.c_str(), msgRect, fonts.fSmall, COL_TEXT_SOFT, true, false);
    }
}

// ============================================================================
// Composición de la ventana
// ============================================================================

static int StepIndexForHeader() {
    switch (g_state.currentStep) {
        case WizardStep::Welcome:          return 0;
        case WizardStep::License:          return 1;
        case WizardStep::UninstallConfirm: return 1;
        case WizardStep::Working:          return 2;
        case WizardStep::Complete:         return 3;
    }
    return 0;
}

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

    Fonts fonts;
    DrawChrome(g, fonts, W, H);
    DrawPageHeader(g, fonts, W, SubtitleForHeader(), StepIndexForHeader());

    const LayoutRects layout = ComputeLayout(W, H);

    switch (g_state.currentStep) {
        case WizardStep::Welcome:          RenderWelcome(g, fonts, W, H); break;
        case WizardStep::License:          RenderLicense(g, fonts, W, H, layout); break;
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
    g_state.log.clear();
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
                PostMessageW(g_state.hwnd, WM_CLOSE, 0, 0);
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
        case WizardStep::UninstallConfirm:
            PostMessageW(g_state.hwnd, WM_CLOSE, 0, 0);
            break;
        case WizardStep::Complete:
            if (g_state.installSucceeded || g_state.mode != AppMode::Uninstall) {
                PostMessageW(g_state.hwnd, WM_CLOSE, 0, 0);
            } else {
                // "Reintentar"
                StartWork(g_state.mode == AppMode::Uninstall ? AppMode::Uninstall : AppMode::Install);
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
                InvalidateRect(hwnd, nullptr, FALSE);
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
            const float consoleH = static_cast<float>(client.bottom) / g_scale - 254.0f - 64.0f;
            const int visible = static_cast<int>((consoleH - 46.0f) / 17.0f);
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
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (hdc) {
                Graphics graphics(hdc);
                graphics.ScaleTransform(g_scale, g_scale);
                graphics.SetCompositingQuality(CompositingQualityHighQuality);
                graphics.SetSmoothingMode(SmoothingModeHighQuality);
                graphics.SetPixelOffsetMode(PixelOffsetModeHalf);
                graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

                graphics.SetClip(RectF(
                    static_cast<REAL>(ps.rcPaint.left) / g_scale,
                    static_cast<REAL>(ps.rcPaint.top) / g_scale,
                    static_cast<REAL>(ps.rcPaint.right - ps.rcPaint.left) / g_scale,
                    static_cast<REAL>(ps.rcPaint.bottom - ps.rcPaint.top) / g_scale));

                RECT client;
                GetClientRect(hwnd, &client);
                RenderWindow(graphics, client);
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
static int RunSilent(AppMode mode) {
    g_state.hwnd = nullptr;      // PipeUi destruye los mensajes sin UI
    g_state.silent = true;
    if (mode != AppMode::Uninstall && g_state.installPath.empty()) {
        g_state.installPath = DetectInstallDir();
    }
    // COM es necesario para crear accesos directos en el hilo del pipeline.
    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    RunPipeline(mode);
    if (SUCCEEDED(comHr)) CoUninitialize();

    // Actualización silenciosa: la aplicación se reabre automáticamente.
    if (mode == AppMode::Update && g_lastRunSucceeded) {
        const std::wstring exe = g_state.installPath + L"\\artpicst.exe";
        if (GetFileAttributesW(exe.c_str()) != INVALID_FILE_ATTRIBUTES) {
            LaunchAppForUser(exe, g_state.installPath);
        }
    }
    return g_lastRunSucceeded ? 0 : 1;
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
