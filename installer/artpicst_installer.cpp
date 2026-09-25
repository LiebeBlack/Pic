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
#include <wchar.h>
#include <fstream>
#include <sstream>

// Directivas de enlace de MSVC, aisladas para que GCC/Clang compilen sin avisos.
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

const wchar_t APP_NAME[] = L"ARTPICST";
const wchar_t APP_VERSION[] = L"1.2.0";
const wchar_t CLASS_NAME[] = L"ARTPICSTInstallerWindow";
const wchar_t UNINSTALL_SWITCH[] = L"--uninstall";
const wchar_t UNINSTALL_REG_KEY[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ARTPICST";
const wchar_t APP_URL[] = L"https://github.com/LiebeBlack/Pic";

// ============================================================================
// Paleta "Pitch-Black Neon": fondo negro profundo, acentos cian/violeta.
// Toda la interfaz deriva de estas constantes (no hay colores dispersos).
// ============================================================================
const Color COL_TEXT(255, 236, 240, 246);          // Texto principal
const Color COL_TEXT_SOFT(255, 158, 168, 184);     // Texto secundario
const Color COL_TEXT_DIM(255, 104, 114, 132);      // Texto terciario / etiquetas
const Color COL_PANEL(255, 13, 15, 20);            // Tarjetas / paneles
const Color COL_PANEL_BORDER(255, 34, 40, 52);     // Borde de tarjetas
const Color COL_BTN_GHOST(255, 22, 26, 34);        // Botón secundario
const Color COL_BTN_GHOST_HOT(255, 32, 38, 50);
const Color COL_BTN_GHOST_BORDER(255, 46, 54, 70);
const Color COL_BTN_GHOST_BORDER_HOT(255, 0, 210, 255);   // Cian al pasar el cursor
const Color COL_ACCENT_A(255, 0, 224, 255);        // Acento: cian neón
const Color COL_ACCENT_B(255, 158, 74, 255);       // Acento: violeta
const Color COL_SUCCESS(255, 46, 220, 130);
const Color COL_ERROR(255, 255, 82, 92);
const Color COL_DISABLED_TEXT(255, 96, 104, 120);

// Tamaño de diseño (unidades lógicas 96 DPI); la ventana se escala por g_scale.
// Compacto: 560x480 reduce el área repintada y sitúa todo el contenido por
// encima del pliegue sin scroll en pantallas de portátil.
const float DESIGN_W = 560.0f;
const float DESIGN_H = 480.0f;
const float MIN_DESIGN_W = 520.0f;
// Mínimo = alto de diseño: las filas de opciones terminan en y=396 y los
// botones viven en y=H-58; por debajo de 480 se solaparían.
const float MIN_DESIGN_H = 480.0f;
float g_scale = 1.0f; // factor DPI real / 96

enum class InstallStep {
    Welcome,
    License,
    Install,
    Complete
};

struct InstallerState {
    HWND hwnd = nullptr;
    HINSTANCE hInstance = nullptr;
    InstallStep currentStep = InstallStep::Welcome;
    std::wstring installPath;
    std::wstring installStatus = L"Preparando la instalación...";
    bool createDesktopShortcut = true;
    bool createStartMenuShortcut = true;
    bool registerFileAssociations = true;
    bool isInstalling = false;
    bool installSucceeded = false;
    int installProgress = 0;
    int hoverZone = 0;
    bool mouseTracking = false;
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken = 0;
};

InstallerState g_state;

// Zonas hover de la interfaz
enum HoverZone {
    HOVER_NONE = 0,
    HOVER_BACK,
    HOVER_NEXT,
    HOVER_CANCEL,
    HOVER_ROW_DESKTOP,
    HOVER_ROW_STARTMENU,
    HOVER_ROW_ASSOC
};

// Una sola fuente de verdad para la geometría (dibujo, clic y hover)
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
    const float buttonH = 40.0f;
    const float buttonY = H - buttonH - 18.0f;   // anclado al pie: se adapta a H

    // Botón primario (derecha) y secundario Atrás (izquierda)
    r.next = RectF(W - margin - 144.0f, buttonY, 144.0f, buttonH);
    r.back = RectF(margin, buttonY, 112.0f, buttonH);

    // Cancelar discreto (arriba a la derecha)
    r.cancel = RectF(W - margin - 76.0f, 12.0f, 76.0f, 24.0f);

    // Filas de opciones de la página de licencia (terminan en y=396, por
    // encima del texto de destino en H-72 y de los botones en H-58)
    if (g_state.currentStep == InstallStep::License) {
        const float rowX = 52.0f;
        const float rowW = W - rowX * 2.0f;
        const float rowH = 32.0f;
        const float gap = 6.0f;
        float y = 288.0f;
        for (int i = 0; i < 3; ++i) {
            r.rows[i] = RectF(rowX, y, rowW, rowH);
            y += rowH + gap;
        }
        r.rowCount = 3;
    }
    return r;
}

int HoverZoneAt(const LayoutRects& r, float lx, float ly) {
    if (g_state.isInstalling) return HOVER_NONE;   // durante la instalación no hay botones vivos
    auto hit = [&](const RectF& rc) {
        return lx >= rc.X && lx <= rc.X + rc.Width && ly >= rc.Y && ly <= rc.Y + rc.Height;
    };
    if (hit(r.cancel)) return HOVER_CANCEL;
    if (g_state.currentStep == InstallStep::License && hit(r.back)) return HOVER_BACK;
    if (g_state.currentStep == InstallStep::License) {
        for (int i = 0; i < r.rowCount; ++i) {
            if (hit(r.rows[i])) return HOVER_ROW_DESKTOP + i;
        }
    }
    if ((g_state.currentStep == InstallStep::Welcome ||
         g_state.currentStep == InstallStep::License ||
         g_state.currentStep == InstallStep::Complete) && hit(r.next)) {
        return HOVER_NEXT;
    }
    return HOVER_NONE;
}

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
    std::wstring full = GetModulePath();
    size_t pos = full.find_last_of(L'\\');
    if (pos == std::wstring::npos) return {};
    return full.substr(0, pos);
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
    // Reserva (políticas restrictivas o sistemas sin Program Files): perfil.
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

// Crea todos los directorios intermedios de una ruta ("C:\A\B\C" -> C, C\A, ...)
bool CreateDirectoryTree(const std::wstring& path) {
    if (path.empty()) return false;
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const int result = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    return result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS;
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

// ============================================================================
// Instalación tradicional: elevación, raíz de registro y dependencias
// ============================================================================
// A partir de esta versión la instalación es TRADICIONAL (machine-wide):
//   · %ProgramFiles%\ARTPICST con archivos, accesos directos y menú Inicio,
//   · claves en HKLM (Software\ARTPICST, Uninstall, Classes, App Paths,
//     RegisteredApplications/Capabilities),
//   · comprobación e instalación del Microsoft Visual C++ Redistributable.
// Si el proceso no está elevado se relanza a sí mismo con el verbo "runas"
// (UAC); si el usuario rechaza la elevación se degrada automáticamente a una
// instalación por usuario (HKCU + %LOCALAPPDATA%) para no quedar inservible.

bool g_machineWide = false;   // true -> HKLM / Program Files, false -> HKCU

HKEY RegRoot() { return g_machineWide ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER; }

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

// El runtime de Visual C++ (2015-2022) se detecta por registro y, como respaldo,
// intentando cargar la CRT como datos (no se ejecuta código).
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

// Busca el paquete redistribuible junto al instalador (o en .\redist).
std::wstring FindRedistributablePayload(const std::wstring& srcDir) {
    const wchar_t* names[] = { L"vc_redist.x64.exe", L"vcredist_x64.exe" };
    const std::wstring roots[] = { srcDir, srcDir + L"\\redist" };
    for (const auto& root : roots) {
        for (const wchar_t* name : names) {
            const std::wstring candidate = root + L"\\" + name;
            if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) return candidate;
        }
    }
    return {};
}

// Instala el redistribuible en modo silencioso si falta. 1638 = versión ya
// instalada y 3010 = correcto pero requiere reiniciar: ambos se consideran éxito.
bool InstallVCRedistributable(const std::wstring& srcDir, std::wstring& outStatus) {
    if (IsVCRuntimeInstalled()) {
        outStatus = L"Microsoft Visual C++ 2015-2022 (x64): ya está instalado.";
        return true;
    }
    const std::wstring payload = FindRedistributablePayload(srcDir);
    if (payload.empty()) {
        // La aplicación se compila con la CRT estática: no es un bloqueo.
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
    const bool ok = (exitCode == 0 || exitCode == 1638 || exitCode == 3010);
    if (exitCode == 3010) {
        outStatus = L"Visual C++ Redistributable instalado (requiere reiniciar Windows).";
    } else if (exitCode == 1638) {
        outStatus = L"Visual C++ Redistributable: ya presente o versión superior.";
    } else {
        outStatus = ok ? L"Visual C++ Redistributable instalado correctamente."
                       : L"Aviso: el Visual C++ Redistributable devolvió un código de error.";
    }
    return ok;
}

// Copia las DLL que viajen junto al instalador (dependencias aceleradoras,
// complementos o bibliotecas propias de la aplicación).
int CopyBundledDlls(const std::wstring& srcDir, const std::wstring& dstDir) {
    WIN32_FIND_DATAW data{};
    const std::wstring pattern = srcDir + L"\\*.dll";
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return 0;
    int copied = 0;
    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
        const std::wstring src = srcDir + L"\\" + data.cFileName;
        const std::wstring dst = dstDir + L"\\" + data.cFileName;
        if (CopyFileIfExists(src, dst)) ++copied;
        if (g_machineWide) {
            // Las DLL deben quedar registradas para el sistema tras la copia.
            std::wstring cmd = L"regsvr32.exe /s \"" + dst + L"\"";
            std::vector<wchar_t> buffer(cmd.begin(), cmd.end());
            buffer.push_back(L'\0');
            STARTUPINFOW si{};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};
            if (CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE,
                               CREATE_NO_WINDOW, nullptr, dstDir.c_str(), &si, &pi)) {
                WaitForSingleObject(pi.hProcess, 30u * 1000u);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            }
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return copied;
}

const std::wstring& AppKeyPath() {
    static const std::wstring key = L"Software\\" + std::wstring(APP_NAME);
    return key;
}

// ============================================================================
// Asociaciones de archivo respetando los controladores de miniaturas
// ============================================================================
// Se registra el ProgID propio y se añade ARTPICST a "Abrir con" de cada
// extensión mediante OpenWithProgids, SIN sobrescribir el valor predeterminado
// de la extensión: así el Explorador conserva su proveedor nativo de miniaturas
// (Shell Thumbnail Provider) y las miniaturas de las imágenes siguen viéndose.

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

// Elimina un valor del registro solo si coincide EXACTAMENTE con el esperado:
// jamás se borra una asociación que no sea nuestra.
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
    // App Paths: permite lanzar "artpicst" desde Ejecutar o la consola.
    ok = ok && SetRegStringValue(root, appPaths, L"", exePath);
    ok = ok && SetRegStringValue(root, appPaths, L"Path", ParentDirectory(exePath));

    // Extensión por extensión: se añade ARTPICST a "Abrir con" y se declara la
    // capacidad, SIN tocar el valor predeterminado de la extensión.
    const std::wstring caps = AppKeyPath() + L"\\Capabilities";
    for (const auto& ext : SupportedAssociationExtensions()) {
        ok = ok && SetRegStringValue(root, L"Software\\Classes\\" + ext + L"\\OpenWithProgids", fileType, L"");
        ok = ok && SetRegStringValue(root, caps + L"\\FileAssociations", ext, fileType);
    }
    // Registro en "Aplicaciones predeterminadas" de Windows 10/11
    ok = ok && SetRegStringValue(root, caps, L"ApplicationName", APP_NAME);
    ok = ok && SetRegStringValue(root, caps, L"ApplicationDescription",
                                 L"Visor de imagenes ARTPICST: maxima calidad y fluidez");
    ok = ok && SetRegStringValue(root, L"Software\\RegisteredApplications", APP_NAME, caps);
    // Documentación del desinstalador (para auditoría y soporte)
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
    const std::wstring uninstallCmd = L"\"" + installDir + L"\\artpicst_installer.exe\" " + UNINSTALL_SWITCH;
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
    // Windows muestra el tamaño en "Aplicaciones instaladas" (en KiB).
    const uint64_t bytes = DirectorySizeBytes(installDir);
    if (bytes > 0) {
        ok = ok && SetRegDwordValue(root, UNINSTALL_REG_KEY, L"EstimatedSize",
                                    static_cast<DWORD>((bytes + 1023ull) / 1024ull));
    }
    return ok;
}

// ============================================================================
// Desinstalación
// ============================================================================

void RemoveAppShortcutsIn(const std::wstring& folder) {
    if (folder.empty()) return;
    DeleteFileW((folder + L"\\ARTPICST.lnk").c_str());
    const std::wstring menuDir = folder + L"\\ARTPICST";
    DeleteFileW((menuDir + L"\\ARTPICST.lnk").c_str());
    DeleteFileW((menuDir + L"\\Uninstall ARTPICST.lnk").c_str());
    RemoveDirectoryW(menuDir.c_str());
}

bool PerformUninstall() {
    const std::wstring installDir = GetModuleFolder();
    const std::wstring selfPath = GetModulePath();
    if (installDir.empty() || selfPath.empty()) return false;

    // 1) Accesos directos: perfil del usuario actual y perfil "Todos los usuarios"
    RemoveAppShortcutsIn(GetShellFolder(CSIDL_DESKTOPDIRECTORY));
    RemoveAppShortcutsIn(GetShellFolder(CSIDL_COMMON_DESKTOPDIRECTORY));
    RemoveAppShortcutsIn(GetShellFolder(CSIDL_PROGRAMS));
    RemoveAppShortcutsIn(GetShellFolder(CSIDL_COMMON_PROGRAMS));

    // 2) Registro: se limpian AMBAS raíces (las versiones antiguas instalaban
    //    solo por usuario) y NUNCA se borra una asociación que no sea nuestra.
    const HKEY roots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
    for (HKEY root : roots) {
        RegDeleteTreeW(root, UNINSTALL_REG_KEY);
        RegDeleteTreeW(root, AppKeyPath().c_str());
        RegDeleteTreeW(root, L"Software\\Classes\\ARTPICST.Image");
        RegDeleteTreeW(root, L"Software\\Classes\\Applications\\artpicst.exe");
        RegDeleteTreeW(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\artpicst.exe");
        DeleteRegValue(root, L"Software\\RegisteredApplications", APP_NAME);
        for (const auto& ext : SupportedAssociationExtensions()) {
            const std::wstring extKey = L"Software\\Classes\\" + ext;
            // Solo se retira NUESTRA entrada de "Abrir con": la clave de la
            // extensión y su proveedor de miniaturas quedan intactos, así que
            // las miniaturas del Explorador siguen funcionando.
            DeleteRegValue(root, extKey + L"\\OpenWithProgids", L"ARTPICST.Image");
            // Limpieza de instalaciones antiguas que sí secuestraban el valor
            // predeterminado de la extensión (solo si es exactamente el nuestro).
            DeleteRegValueIfEquals(root, extKey, L"", L"ARTPICST.Image");
        }
    }

    // 3) Archivos: se recorren TODOS los que haya en la carpeta (incluidas las
    //    DLL y el redistribuible copiados por el instalador).
    WIN32_FIND_DATAW entry{};
    HANDLE find = FindFirstFileW((installDir + L"\\*").c_str(), &entry);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
            const std::wstring file = installDir + L"\\" + entry.cFileName;
            if (_wcsicmp(file.c_str(), selfPath.c_str()) != 0) DeleteFileW(file.c_str());
        } while (FindNextFileW(find, &entry));
        FindClose(find);
    }

    // Renombrar el ejecutable en ejecución y programar la limpieza final
    wchar_t tempDir[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, tempDir) == 0) return false;
    std::wstring movedSelf = std::wstring(tempDir) + L"artpicst_uninstaller_" + std::to_wstring(GetCurrentProcessId()) + L".exe";
    if (!MoveFileExW(selfPath.c_str(), movedSelf.c_str(), MOVEFILE_REPLACE_EXISTING)) return false;

    // El propio proceso sigue vivo mientras el usuario confirma el desinstalado;
    // se reintenta el borrado del ejecutable movido hasta que el proceso termina.
    std::wstring cmd = L"/c ping 127.0.0.1 -n 2 >nul & del /f /q \"" + movedSelf +
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

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
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

static void DrawCheckMark(Graphics& g, float cx0, float cy0, float cx1, float cy1, float cx2, float cy2, float width, const Color& color) {
    Pen pen(color, width);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    g.DrawLine(&pen, cx0, cy0, cx1, cy1);
    g.DrawLine(&pen, cx1, cy1, cx2, cy2);
}

// Caja de verificación cuadrada de las filas de opciones
static void DrawCheckBox(Graphics& g, const RectF& rc, bool checked) {
    if (checked) {
        // Degradado cian -> violeta sobre la casilla activa
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

// ============================================================================
// Textos y fuentes reutilizables (UnitPixel: escalan con la transformación DPI)
// ============================================================================

struct Fonts {
    FontFamily family;
    Font fLogo;      // 26 px
    Font fTitle;     // 24 px
    Font fHeading;   // 18 px
    Font fBody;      // 13 px
    Font fSmall;     // 12.5 px
    Font fLabel;     // 10.5 px, negrita (etiquetas)
    Font fTiny;      // 10 px
    Fonts()
        : family(L"Segoe UI"),
          fLogo(&family, 26.0f, FontStyleBold, UnitPixel),
          fTitle(&family, 24.0f, FontStyleBold, UnitPixel),
          fHeading(&family, 18.0f, FontStyleBold, UnitPixel),
          fBody(&family, 13.0f, FontStyleRegular, UnitPixel),
          fSmall(&family, 12.5f, FontStyleRegular, UnitPixel),
          fLabel(&family, 10.5f, FontStyleBold, UnitPixel),
          fTiny(&family, 10.0f, FontStyleRegular, UnitPixel) {}
};

// Logo: baldosa redondeada con degradado cian->violeta y la letra inicial
static void DrawLogo(Graphics& g, const Fonts& fonts, float cx, float cy, float size) {
    const RectF tile(cx - size * 0.5f, cy - size * 0.5f, size, size);
    FillRoundGradient(g, tile, size * 0.24f, COL_ACCENT_A, COL_ACCENT_B);
    // Brillo superior sutil (efecto cristal)
    const RectF shine(tile.X, tile.Y, tile.Width, tile.Height * 0.5f);
    FillRound(g, shine, size * 0.24f, Color(28, 255, 255, 255));
    DrawTextIn(g, L"A", tile, fonts.fLogo, Color(255, 255, 255, 255), true, true);
}

// Fondo general: degradado vertical "pitch-black" + barra de acento superior
static void DrawChrome(Graphics& g, const Fonts&, float W, float H) {
    LinearGradientBrush bg(RectF(0.0f, 0.0f, W, H), Color(255, 8, 9, 12), Color(255, 0, 0, 0), 90.0f);
    g.FillRectangle(&bg, 0.0f, 0.0f, W, H);

    // Barra superior degradada (firma de la marca: cian -> violeta)
    const RectF topBar(0.0f, 0.0f, W, 3.0f);
    LinearGradientBrush accent(topBar, COL_ACCENT_A, COL_ACCENT_B, 0.0f);
    g.FillRectangle(&accent, topBar);
}

// Botón primario con degradado cian -> violeta
static void DrawPrimaryButton(Graphics& g, const Fonts& fonts, const RectF& rc, const wchar_t* text, bool hot) {
    FillRoundGradient(g, rc, 8.0f, COL_ACCENT_A, COL_ACCENT_B);
    if (hot) {
        FillRound(g, rc, 8.0f, Color(26, 255, 255, 255)); // realce hover
    }
    DrawTextIn(g, text, rc, fonts.fSmall, Color(255, 255, 255, 255), true, true);
}

// Botón secundario discreto (Atrás / Cerrar / Reintentar / Cancelar)
static void DrawGhostButton(Graphics& g, const Fonts& fonts, const RectF& rc, const wchar_t* text, bool hot) {
    FillRound(g, rc, 8.0f, hot ? COL_BTN_GHOST_HOT : COL_BTN_GHOST);
    StrokeRound(g, rc, 8.0f, hot ? COL_BTN_GHOST_BORDER_HOT : COL_BTN_GHOST_BORDER, 1.0f);
    DrawTextIn(g, text, rc, fonts.fSmall, hot ? COL_TEXT : COL_TEXT_SOFT, true, true);
}

// ============================================================================
// Pantallas del asistente
// ============================================================================

void RenderWelcome(Graphics& g, const Fonts& fonts, float W, float H) {
    const float cx = W * 0.5f;

    DrawLogo(g, fonts, cx, 84.0f, 56.0f);

    RectF nameRect(cx - 180.0f, 118.0f, 360.0f, 40.0f);
    DrawTextIn(g, APP_NAME, nameRect, fonts.fTitle, COL_TEXT, true, false);

    RectF tagRect(cx - 240.0f, 162.0f, 480.0f, 22.0f);
    DrawTextIn(g, L"Visor de imágenes premium para Windows — rápido, ligero y moderno",
               tagRect, fonts.fSmall, COL_TEXT_SOFT, true, true);

    // Tarjeta de características (anclada al contenido, no al borde inferior)
    const float cardY = 202.0f;
    const RectF card(cx - 224.0f, cardY, 448.0f, 148.0f);
    FillRound(g, card, 14.0f, COL_PANEL);
    StrokeRound(g, card, 14.0f, COL_PANEL_BORDER, 1.0f);

    struct Feature { const wchar_t* text; };
    const Feature features[] = {
        { L"Más de 30 formatos: PNG, WebP, HEIC, AVIF, GIF, RAW y más" },
        { L"Zoom fluido por GPU, píxel perfecto al 100% y Ultra-Claridad HDR" },
        { L"Interfaz oscura elegante y consumo mínimo de RAM y CPU" },
        { L"Instalación tradicional: Program Files, menú Inicio y desinstalador" }
    };

    float fy = cardY + 20.0f;
    for (int i = 0; i < 4; ++i) {
        // Punto de acento degradado
        const float dotR = 3.5f;
        const float dotY = fy + 8.0f;
        FillRoundGradient(g, RectF(card.X + 22.0f, dotY - dotR, dotR * 2.0f, dotR * 2.0f), dotR, COL_ACCENT_A, COL_ACCENT_B);
        RectF featureRect(card.X + 38.0f, fy - 2.0f, card.Width - 54.0f, 22.0f);
        DrawTextIn(g, features[i].text, featureRect, fonts.fSmall, COL_TEXT_SOFT, false, true);
        fy += 30.0f;
    }

    RectF hint(cx - 210.0f, H - 76.0f, 420.0f, 18.0f);
    DrawTextIn(g, (g_machineWide
                       ? L"Se instalará para todos los usuarios (requiere administrador)."
                       : L"Se instalará para tu usuario, sin permisos de administrador."),
               hint, fonts.fTiny, COL_TEXT_DIM, true, true);
}

void RenderLicense(Graphics& g, const Fonts& fonts, float W, float H, const LayoutRects& layout) {
    const float cx = W * 0.5f;

    RectF titleRect(cx - 220.0f, 36.0f, 440.0f, 36.0f);
    DrawTextIn(g, L"Licencia y opciones", titleRect, fonts.fHeading, COL_TEXT, true, true);

    // Caja del acuerdo (compacta: el texto usa fTiny con ajuste adaptativo)
    const RectF box(cx - 232.0f, 82.0f, 464.0f, 138.0f);
    FillRound(g, box, 12.0f, Color(255, 10, 12, 17));
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

    // Ajuste adaptativo: si el texto no cabe, se reduce el tamaño de fuente
    const float textW = box.Width - 36.0f;
    const float textH = box.Height - 34.0f;
    const RectF textArea(box.X + 18.0f, box.Y + 32.0f, textW, textH);
    float fontSize = 11.0f;
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

    // Opciones de instalación (fila y = 288 según ComputeLayout)
    RectF optTitle(52.0f, 266.0f, W - 104.0f, 14.0f);
    DrawTextIn(g, L"OPCIONES DE INSTALACIÓN", optTitle, fonts.fLabel, COL_TEXT_DIM, false, true);

    struct OptionRow { const wchar_t* label; bool* value; int hover; };
    const OptionRow rows[] = {
        { L"Crear acceso directo en el Escritorio", &g_state.createDesktopShortcut, HOVER_ROW_DESKTOP },
        { L"Crear acceso directo en el Menú Inicio", &g_state.createStartMenuShortcut, HOVER_ROW_STARTMENU },
        { L"Asociar formatos de imagen a ARTPICST", &g_state.registerFileAssociations, HOVER_ROW_ASSOC }
    };

    for (int i = 0; i < 3; ++i) {
        const RectF& row = layout.rows[i];
        const bool hot = g_state.hoverZone == rows[i].hover;
        FillRound(g, row, 8.0f, hot ? Color(255, 24, 30, 42) : Color(255, 15, 18, 25));
        StrokeRound(g, row, 8.0f, hot ? COL_BTN_GHOST_BORDER_HOT : COL_PANEL_BORDER, 1.0f);

        const RectF boxRect(row.X + 12.0f, row.Y + (row.Height - 18.0f) * 0.5f, 18.0f, 18.0f);
        DrawCheckBox(g, boxRect, *rows[i].value);

        RectF labelRect(row.X + 40.0f, row.Y, row.Width - 50.0f, row.Height);
        DrawTextIn(g, rows[i].label, labelRect, fonts.fSmall, COL_TEXT, false, true);
    }

    // Ruta de destino, anclada al pie (y=H-72): las filas terminan en y=396 y
    // los botones viven en H-58, así que no hay solape en ningún ancho.
    RectF destRect(52.0f, H - 72.0f, W - 104.0f, 18.0f);
    std::wstring dest = L"Se instalará en:  " + g_state.installPath;
    DrawTextIn(g, dest.c_str(), destRect, fonts.fTiny, COL_TEXT_DIM, false, true,
               StringTrimmingEllipsisCharacter, true);
}

void RenderInstall(Graphics& g, const Fonts& fonts, float W, float H) {
    const float cx = W * 0.5f;

    DrawLogo(g, fonts, cx, 104.0f, 48.0f);

    RectF titleRect(cx - 220.0f, 134.0f, 440.0f, 34.0f);
    DrawTextIn(g, L"Instalando ARTPICST", titleRect, fonts.fHeading, COL_TEXT, true, true);

    RectF subRect(cx - 240.0f, 170.0f, 480.0f, 20.0f);
    DrawTextIn(g, L"Se está instalando en tu equipo. No cierres esta ventana.",
               subRect, fonts.fSmall, COL_TEXT_SOFT, true, true);

    // Barra de progreso
    const float barY = 212.0f;
    const RectF track(cx - 200.0f, barY, 400.0f, 10.0f);
    FillRound(g, track, 5.0f, Color(255, 18, 22, 30));
    StrokeRound(g, track, 5.0f, Color(255, 36, 44, 58), 1.0f);

    if (g_state.installProgress > 0) {
        const float fillW = track.Width * (g_state.installProgress / 100.0f);
        if (fillW > 1.0f) {
            const RectF fill(track.X, track.Y, fillW, track.Height);
            Region clip(fill);
            g.SetClip(&clip);
            FillRoundGradient(g, track, 5.0f, COL_ACCENT_A, COL_ACCENT_B);
            g.ResetClip();
        }
    }

    // Estado y porcentaje
    RectF statusRect(cx - 240.0f, barY + 24.0f, 480.0f, 22.0f);
    DrawTextIn(g, g_state.installStatus.c_str(), statusRect, fonts.fSmall, COL_TEXT, true, true);

    wchar_t percentText[32];
    swprintf_s(percentText, 32, L"%d%%", g_state.installProgress);
    RectF pctRect(cx - 240.0f, barY + 48.0f, 480.0f, 18.0f);
    DrawTextIn(g, percentText, pctRect, fonts.fLabel, COL_TEXT_SOFT, true, true);

    // Ruta de destino, anclada al pie
    std::wstring dest = L"Destino: " + g_state.installPath;
    RectF destRect(cx - 220.0f, H - 76.0f, 440.0f, 18.0f);
    DrawTextIn(g, dest.c_str(), destRect, fonts.fTiny, COL_TEXT_DIM, true, true, StringTrimmingEllipsisCharacter, true);
}

void RenderComplete(Graphics& g, const Fonts& fonts, float W) {
    const float cx = W * 0.5f;

    if (g_state.installSucceeded) {
        // Anillo verde con check
        const float r = 34.0f;
        const float cy = 116.0f;
        const RectF ring(cx - r, cy - r, r * 2.0f, r * 2.0f);
        GraphicsPath ringPath;
        RoundPath(ringPath, ring, r);
        SolidBrush ringBrush(COL_SUCCESS);
        g.FillPath(&ringBrush, &ringPath);
        DrawCheckMark(g, cx - 14.0f, cy + 1.0f, cx - 4.0f, cy + 11.0f, cx + 15.0f, cy - 11.0f, 4.0f, Color(255, 255, 255, 255));

        RectF titleRect(cx - 220.0f, 178.0f, 440.0f, 36.0f);
        DrawTextIn(g, L"Instalación completada", titleRect, fonts.fTitle, COL_TEXT, true, true);

        RectF subRect(cx - 220.0f, 218.0f, 440.0f, 20.0f);
        DrawTextIn(g, L"Gracias por elegir ARTPICST.", subRect, fonts.fSmall, COL_TEXT_SOFT, true, true);

        // Tarjeta con la ruta
        const RectF card(cx - 204.0f, 254.0f, 408.0f, 88.0f);
        FillRound(g, card, 12.0f, COL_PANEL);
        StrokeRound(g, card, 12.0f, COL_PANEL_BORDER, 1.0f);

        RectF cardLabel(card.X + 18.0f, card.Y + 12.0f, card.Width - 36.0f, 14.0f);
        DrawTextIn(g, L"UBICACIÓN DE INSTALACIÓN", cardLabel, fonts.fLabel, COL_TEXT_DIM, false, true);

        RectF cardPath(card.X + 18.0f, card.Y + 33.0f, card.Width - 36.0f, 22.0f);
        DrawTextIn(g, g_state.installPath.c_str(), cardPath, fonts.fSmall, COL_TEXT, false, true,
                   StringTrimmingEllipsisCharacter, true);

        RectF cardVer(card.X + 18.0f, card.Y + 60.0f, card.Width - 36.0f, 14.0f);
        std::wstring ver = std::wstring(L"Versión ") + APP_VERSION +
                           (g_machineWide ? L"  ·  Para todos los usuarios"
                                          : L"  ·  Sólo para este usuario");
        DrawTextIn(g, ver.c_str(), cardVer, fonts.fTiny, COL_TEXT_SOFT, false, true);
    } else {
        // Anillo rojo con signo de exclamación
        const float r = 34.0f;
        const float cy = 116.0f;
        const RectF ring(cx - r, cy - r, r * 2.0f, r * 2.0f);
        GraphicsPath ringPath;
        RoundPath(ringPath, ring, r);
        SolidBrush ringBrush(COL_ERROR);
        g.FillPath(&ringBrush, &ringPath);
        Font bangFont(&fonts.family, 38.0f, FontStyleBold, UnitPixel);
        DrawTextIn(g, L"!", ring, bangFont, Color(255, 255, 255, 255), true, true);

        RectF titleRect(cx - 220.0f, 174.0f, 440.0f, 36.0f);
        DrawTextIn(g, L"No se pudo completar la instalación", titleRect, fonts.fTitle, COL_TEXT, true, true);

        RectF msgRect(cx - 220.0f, 218.0f, 440.0f, 60.0f);
        DrawTextIn(g, L"Coloca el instalador en la misma carpeta que artpicst.exe\n"
                       L"(con artpicst.ico, version.json y las DLL que lo acompañen)"
                       L" y vuelve a intentarlo.",
                   msgRect, fonts.fSmall, COL_TEXT_SOFT, true, false);
    }
}

// ============================================================================
// Composición de la ventana
// ============================================================================

void RenderWindow(Graphics& g, const RECT& client) {
    const float W = static_cast<float>(client.right) / g_scale;
    const float H = static_cast<float>(client.bottom) / g_scale;

    Fonts fonts;
    DrawChrome(g, fonts, W, H);

    const LayoutRects layout = ComputeLayout(W, H);

    switch (g_state.currentStep) {
        case InstallStep::Welcome:
            RenderWelcome(g, fonts, W, H);
            break;
        case InstallStep::License:
            RenderLicense(g, fonts, W, H, layout);
            break;
        case InstallStep::Install:
            RenderInstall(g, fonts, W, H);
            break;
        case InstallStep::Complete:
            RenderComplete(g, fonts, W);
            break;
    }

    // Cancelar (arriba a la derecha); se oculta mientras la instalación está en
    // curso (isInstalling) para que no se pueda abortar a medias.
    if (!g_state.isInstalling) {
        DrawGhostButton(g, fonts, layout.cancel, L"Cancelar", g_state.hoverZone == HOVER_CANCEL);
    }

    // Pie: botones de navegación
    if (g_state.currentStep == InstallStep::Welcome) {
        DrawPrimaryButton(g, fonts, layout.next, L"Siguiente  →", g_state.hoverZone == HOVER_NEXT);
    } else if (g_state.currentStep == InstallStep::License) {
        DrawGhostButton(g, fonts, layout.back, L"←  Volver", g_state.hoverZone == HOVER_BACK);
        DrawPrimaryButton(g, fonts, layout.next, L"Instalar", g_state.hoverZone == HOVER_NEXT);
    } else if (g_state.currentStep == InstallStep::Complete) {
        if (g_state.installSucceeded) {
            DrawGhostButton(g, fonts, layout.back, L"Cerrar", g_state.hoverZone == HOVER_BACK);
            DrawPrimaryButton(g, fonts, layout.next, L"Iniciar ARTPICST", g_state.hoverZone == HOVER_NEXT);
        } else {
            DrawGhostButton(g, fonts, layout.back, L"Reintentar", g_state.hoverZone == HOVER_BACK);
            DrawPrimaryButton(g, fonts, layout.next, L"Cerrar", g_state.hoverZone == HOVER_NEXT);
        }
    }
}

// ============================================================================
// Instalación real
// ============================================================================

void PerformInstallation() {
    if (g_state.isInstalling) return; // evita dobles clics / Enter repetido
    g_state.isInstalling = true;
    g_state.currentStep = InstallStep::Install;
    g_state.hoverZone = HOVER_NONE;
    g_state.installProgress = 0;
    InvalidateRect(g_state.hwnd, nullptr, FALSE);

    const std::wstring srcDir = GetModuleFolder();
    const std::wstring dst = g_state.installPath;
    const std::wstring selfPath = GetModulePath();

    auto step = [](int progress, const wchar_t* status) {
        g_state.installProgress = progress;
        // Se asigna PRIMERO y solo si cambió: el contenido del wstring es lo
        // que el pintado lee en el WM_PAINT sincrono de UpdateWindow, no el
        // puntero del argumento (que deja de ser válido al salir del lambda).
        if (g_state.installStatus != status) g_state.installStatus = status;
        InvalidateRect(g_state.hwnd, nullptr, FALSE);
        UpdateWindow(g_state.hwnd);
    };

    auto rollback = [&dst]() {
        // Reversión limpia: si algo falla a mitad, no se deja basura instalada.
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
        DeleteFileW((dst + L"\\artpicst.exe").c_str());
        DeleteFileW((dst + L"\\artpicst_installer.exe").c_str());
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    };

    bool ok = true;
    std::wstring dependencyStatus;

    step(5, L"Preparando el directorio de instalación...");
    if (dst.empty()) {
        ok = false;
    } else if (!CreateDirectoryTree(dst)) {
        ok = false;
    }

    if (ok) {
        step(14, L"Comprobando archivos de la aplicación...");
        if (GetFileAttributesW((srcDir + L"\\artpicst.exe").c_str()) == INVALID_FILE_ATTRIBUTES) {
            ok = false;
        }
    }

    if (ok) {
        step(24, L"Comprobando las dependencias del sistema (Visual C++)...");
        ok = InstallVCRedistributable(srcDir, dependencyStatus);
    }

    if (ok) {
        step(38, L"Copiando el programa principal...");
        ok = CopyFileIfExists(srcDir + L"\\artpicst.exe", dst + L"\\artpicst.exe");
    }

    if (ok) {
        step(50, L"Copiando recursos, bibliotecas y documentación...");
        ok = CopyFileIfExists(srcDir + L"\\artpicst.ico", dst + L"\\artpicst.ico") &&
             CopyFileIfExists(srcDir + L"\\README.md", dst + L"\\README.md") &&
             CopyFileIfExists(srcDir + L"\\version.json", dst + L"\\version.json") &&
             !selfPath.empty() &&
             CopyFileIfExists(selfPath, dst + L"\\artpicst_installer.exe");
        if (ok) CopyBundledDlls(srcDir, dst);   // dependencias aceleradoras / propias
    }

    if (ok) {
        step(62, L"Creando accesos directos y entradas del menú Inicio...");
        // Menú Inicio y escritorio: "Todos los usuarios" cuando se instala en
        // Program Files (visible para cualquier sesión), perfil propio si no.
        const std::wstring desktop = g_machineWide ? GetShellFolder(CSIDL_COMMON_DESKTOPDIRECTORY)
                                                   : GetShellFolder(CSIDL_DESKTOPDIRECTORY);
        const std::wstring programs = g_machineWide ? GetShellFolder(CSIDL_COMMON_PROGRAMS)
                                                    : GetShellFolder(CSIDL_PROGRAMS);
        if (g_state.createDesktopShortcut && !desktop.empty()) {
            CreateShortcut(desktop + L"\\ARTPICST.lnk", dst + L"\\artpicst.exe", L"", dst);
        }
        if (g_state.createStartMenuShortcut && !programs.empty()) {
            const std::wstring menuDir = programs + L"\\ARTPICST";
            CreateDirectoryW(menuDir.c_str(), nullptr);
            CreateShortcut(menuDir + L"\\ARTPICST.lnk", dst + L"\\artpicst.exe", L"", dst);
            CreateShortcut(menuDir + L"\\Uninstall ARTPICST.lnk", dst + L"\\artpicst_installer.exe", UNINSTALL_SWITCH, dst);
        }
    }

    if (ok) {
        step(76, L"Registrando asociaciones de archivo...");
        ok = !g_state.registerFileAssociations || RegisterFileAssociations(dst + L"\\artpicst.exe");
    }

    if (ok) {
        step(88, L"Registrando el desinstalador y los metadatos...");
        ok = WriteUninstallEntry(dst);
    }

    if (!ok) {
        step(96, L"Revirtiendo los cambios...");
        rollback();
        step(100, L"Error: no se pudo completar la instalación.");
    } else {
        // El temporal vive hasta que InvalidateRect repinta: no encadenar
        // c_str() de una expresión que muere al final del statement.
        g_state.installStatus = L"Instalación completada en " + dst + L". " + dependencyStatus;
        g_state.installProgress = 100;
        InvalidateRect(g_state.hwnd, nullptr, FALSE);
        UpdateWindow(g_state.hwnd);
    }

    g_state.isInstalling = false;
    g_state.installSucceeded = ok;
    g_state.currentStep = InstallStep::Complete;
    g_state.hoverZone = HOVER_NONE;
    InvalidateRect(g_state.hwnd, nullptr, FALSE);
    UpdateWindow(g_state.hwnd);
}

// Acción del botón principal (Siguiente / Instalar / Iniciar / Cerrar)
void InvokePrimaryAction() {
    if (g_state.isInstalling) return;
    switch (g_state.currentStep) {
        case InstallStep::Welcome:
            g_state.currentStep = InstallStep::License;
            g_state.hoverZone = HOVER_NONE;
            InvalidateRect(g_state.hwnd, nullptr, FALSE);
            break;
        case InstallStep::License:
            PerformInstallation();
            break;
        case InstallStep::Complete:
            if (g_state.installSucceeded) {
                const HINSTANCE result = ShellExecuteW(
                    nullptr, L"open", (g_state.installPath + L"\\artpicst.exe").c_str(),
                    nullptr, g_state.installPath.c_str(), SW_SHOWNORMAL);
                if (reinterpret_cast<INT_PTR>(result) <= 32) {
                    MessageBoxW(g_state.hwnd, L"No se pudo iniciar ARTPICST.",
                                APP_NAME, MB_OK | MB_ICONWARNING);
                }
            }
            PostMessageW(g_state.hwnd, WM_CLOSE, 0, 0);
            break;
        default:
            break;
    }
}

void InvokeBackAction() {
    if (g_state.isInstalling) return;
    switch (g_state.currentStep) {
        case InstallStep::License:
            g_state.currentStep = InstallStep::Welcome;
            g_state.hoverZone = HOVER_NONE;
            InvalidateRect(g_state.hwnd, nullptr, FALSE);
            break;
        case InstallStep::Complete:
            if (g_state.installSucceeded) {
                // Botón "Cerrar" del éxito
                PostMessageW(g_state.hwnd, WM_CLOSE, 0, 0);
            } else {
                // Botón "Reintentar" del error
                PerformInstallation();
            }
            break;
        default:
            break;
    }
}

void ToggleOptionAt(int rowIndex) {
    switch (rowIndex) {
        case 0: g_state.createDesktopShortcut = !g_state.createDesktopShortcut; break;
        case 1: g_state.createStartMenuShortcut = !g_state.createStartMenuShortcut; break;
        case 2: g_state.registerFileAssociations = !g_state.registerFileAssociations; break;
        default: return;
    }
    // Repinta solo la fila afectada (la casilla y el borde cambian de estado)
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

            // Barra de título oscura (Windows 10/11)
            BOOL darkMode = TRUE;
            DwmSetWindowAttribute(hwnd, 20, &darkMode, sizeof(BOOL));
            DwmSetWindowAttribute(hwnd, 19, &darkMode, sizeof(BOOL));

            // Esquinas redondeadas estilo Windows 11 (se ignora en versiones antiguas)
            const int cornerPref = 2; // DWMWCP_ROUND
            DwmSetWindowAttribute(hwnd, 33, &cornerPref, sizeof(cornerPref));

            // Tamaño mínimo (escalado por DPI)
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<LPMINMAXINFO>(lParam);
            if (info) {
                info->ptMinTrackSize.x = static_cast<LONG>(MIN_DESIGN_W * g_scale + 0.5f);
                info->ptMinTrackSize.y = static_cast<LONG>(MIN_DESIGN_H * g_scale + 0.5f);
            }
            return 0;
        }
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
        case WM_CLOSE:
            // Nunca cerrar a mitad de instalación (evita instalaciones a medias)
            if (g_state.isInstalling) return 0;
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

                // Recorte a la región sucia (ps.rcPaint, en píxeles físicos ->
                // coordenadas de diseño): el resto de la ventana conserva su
                // frame anterior y no hay "flash" del fondo de clase entre
                // BeginPaint y el repintado completo.
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
            return TRUE;   // el WM_PAINT cubre la región sucia: cero flicker
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
                // Repintado quirúrgico: solo los rectángulos de la zona que se
                // apaga y de la que se enciende (los botones/estados son planos,
                // sin sombras que se derramen). Invalidar la ventana entera en
                // cada WM_MOUSEMOVE es lo que producía el parpadeo fatal.
                RECT client{};
                GetClientRect(hwnd, &client);
                const LayoutRects lr = ComputeLayout(
                    static_cast<float>(client.right) / g_scale,
                    static_cast<float>(client.bottom) / g_scale);
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
                    // De píxeles de diseño a píxeles físicos + 1 px de margen
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
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_KEYDOWN: {
            if (g_state.isInstalling) return 0;
            if (wParam == VK_RETURN) {
                InvokePrimaryAction();
            } else if (wParam == VK_ESCAPE) {
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            if (g_state.isInstalling) return 0;
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

            // Filas de opciones (página de licencia): alternar selección
            if (g_state.currentStep == InstallStep::License) {
                for (int i = 0; i < layout.rowCount; ++i) {
                    if (hit(layout.rows[i], lx, ly)) {
                        ToggleOptionAt(i);
                        return 0;
                    }
                }
            }

            // Botón principal / Atrás / Cancelar. Durante la instalación activa
            // (isInstalling) el botón no se dibuja ni responde: no se puede abortar
            // a medias. En la página "Instalando" ya terminada sí se puede cerrar.
            if (hit(layout.next, lx, ly)) {
                InvokePrimaryAction();
                return 0;
            }
            if (!g_state.isInstalling && hit(layout.cancel, lx, ly)) {
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                return 0;
            }
            if (g_state.currentStep == InstallStep::License && hit(layout.back, lx, ly)) {
                InvokeBackAction();
                return 0;
            }
            if (g_state.currentStep == InstallStep::Complete && hit(layout.back, lx, ly)) {
                InvokeBackAction();
                return 0;
            }
            return 0;
        }
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

// ============================================================================
// Inicio
// ============================================================================

typedef BOOL(WINAPI* PFN_SetProcessDpiAwarenessContext)(HANDLE value);

static void EnableDpiAwareness() {
    // Prioridad 1: PerMonitorV2 (Windows 10 1607+)
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        // Copia de la dirección de la función con memcpy: evita el cambio de tipo de
        // puntero a función (comportamiento indefinido y -Wcast-function-type en GCC).
        PFN_SetProcessDpiAwarenessContext fn = nullptr;
        FARPROC raw = GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        static_assert(sizeof(fn) == sizeof(raw), "tamaño de puntero a función inesperado");
        std::memcpy(&fn, &raw, sizeof(fn));
        if (fn) {
            const HANDLE PMV2 = reinterpret_cast<HANDLE>(-4);
            if (fn(PMV2)) return;
        }
    }
    // Prioridad 2: DPI-aware clásico (Windows Vista+)
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

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    (void)hPrevInstance;

    // Argumentos: --uninstall (desinstalar), --silent (sin diálogos),
    // --elevated (marca interna para no volver a pedir UAC en bucle).
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool uninstallRequested = false;
    bool silent = false;
    bool elevationAttempted = false;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (wcscmp(argv[i], UNINSTALL_SWITCH) == 0) {
                uninstallRequested = true;
            } else if (wcscmp(argv[i], L"--silent") == 0) {
                silent = true;
            } else if (wcscmp(argv[i], L"--elevated") == 0) {
                elevationAttempted = true;
            }
        }
        LocalFree(argv);
    }

    // Elevación (UAC) una sola vez, ANTES de tocar el sistema: la instalación es
    // tradicional (Program Files + HKLM + entradas de "Todos los usuarios") y la
    // desinstalación también necesita privilegios para limpiar HKLM.
    if (IsProcessElevated()) {
        g_machineWide = true;
    } else if (!elevationAttempted && RelaunchElevatedSelf()) {
        return 0;   // el proceso elevado retoma la misma tarea
    } else {
        g_machineWide = false;   // el usuario rechazó UAC: instalación por usuario
    }

    if (uninstallRequested) {
        int answer = IDYES;
        if (!silent) {
            answer = MessageBoxW(nullptr,
                L"¿Desea desinstalar ARTPICST?\n\nSe eliminarán los archivos, los accesos directos, "
                L"las claves de registro y las asociaciones creadas por el instalador.\n"
                L"Las imágenes y sus miniaturas no se verán afectadas.",
                L"Desinstalar ARTPICST", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        }
        if (answer == IDYES) {
            const bool removed = PerformUninstall();
            if (!silent) {
                MessageBoxW(nullptr,
                            removed ? L"ARTPICST ha sido desinstalado correctamente."
                                    : L"No se pudo completar la desinstalación. Cierra cualquier instancia de ARTPICST e inténtalo de nuevo.",
                            removed ? L"Desinstalación completada" : L"Desinstalación incompleta",
                            MB_OK | (removed ? MB_ICONINFORMATION : MB_ICONWARNING));
            }
        }
        return 0;
    }

    // DPI: el diseño se hace en unidades 96 DPI y se escala por g_scale
    EnableDpiAwareness();
    g_scale = GetSystemScale();

    // Limitar la escala inicial para que la ventana compacta quepa en pantallas
    // pequeñas: NUNCA por encima del DPI del sistema (evita el solape de texto:
    // escalar >1 agranda la ventana pero las fuentes con UnitPixel ya escalan
    // solas, duplicando tamaños).
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

    // Una sola instancia del instalador a la vez
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Local\\ARTPICST_Installer_Mutex");
    if (!hMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"El instalador de ARTPICST ya está en ejecución.",
                    L"ARTPICST", MB_OK | MB_ICONINFORMATION);
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    g_state.hInstance = hInstance;
    (void)pCmdLine;

    // Inicializar COM (necesario para crear accesos directos)
    HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comOk = SUCCEEDED(comHr);

    // Inicializar GDI+
    if (GdiplusStartup(&g_state.gdiplusToken, &g_state.gdiplusStartupInput, nullptr) != Ok) {
        if (comOk) CoUninitialize();
        CloseHandle(hMutex);
        MessageBoxW(nullptr, L"No se pudo inicializar la interfaz gráfica.", L"ARTPICST", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Registrar la clase de ventana
    WNDCLASSEXW wc = {};   // inicialización completa: sin -Wmissing-field-initializers
    wc.cbSize = sizeof(wc);
    // CS_HREDRAW/CS_VREDRAW deliberadamente ausentes: invalidan la ventana
    // COMPLETA en cada cambio de tamaño y con el fondo de clase a BLACK_BRUSH
    // producen un marco negro visible antes de cada repintado. El WM_PAINT
    // ya pinta la región sucia entera (RenderWindow sobre ps.rcPaint).
    wc.style = CS_SAVEBITS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    // Fondo de clase NULO: el WM_ERASEBKGND devuelve TRUE y RenderWindow pinta
    // el fondo con la paleta del asistente. Un pincel de clase (antes BLACK)
    // mostraba un marco negro entre BeginPaint y el repintado de GDI+.
    wc.hbrBackground = nullptr;
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;

    if (!RegisterClassExW(&wc)) {
        MessageBoxW(nullptr, L"No se pudo registrar la ventana del instalador.", L"ARTPICST", MB_OK | MB_ICONERROR);
        GdiplusShutdown(g_state.gdiplusToken);
        if (comOk) CoUninitialize();
        CloseHandle(hMutex);
        return 1;
    }

    // Crear la ventana (tamaño en píxeles físicos = diseño × escala DPI)
    const int winW = static_cast<int>(DESIGN_W * g_scale + 0.5f);
    const int winH = static_cast<int>(DESIGN_H * g_scale + 0.5f);
    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        CLASS_NAME,
        L"Instalador de ARTPICST",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        winW, winH,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!hwnd) {
        UnregisterClassW(CLASS_NAME, hInstance);
        GdiplusShutdown(g_state.gdiplusToken);
        if (comOk) CoUninitialize();
        CloseHandle(hMutex);
        MessageBoxW(nullptr, L"No se pudo crear la ventana del instalador.", L"ARTPICST", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Centrar en el área de trabajo de la pantalla principal
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

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    GdiplusShutdown(g_state.gdiplusToken);
    if (comOk) CoUninitialize();
    CloseHandle(hMutex);
    return static_cast<int>(msg.wParam);
}
