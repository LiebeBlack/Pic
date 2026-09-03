#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
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
#include <comdef.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

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

using namespace Gdiplus;

const wchar_t APP_NAME[] = L"ARTPICST";
const wchar_t APP_VERSION[] = L"1.2.0";
const wchar_t CLASS_NAME[] = L"ARTPICSTInstallerWindow";
const wchar_t UNINSTALL_SWITCH[] = L"--uninstall";
const wchar_t UNINSTALL_REG_KEY[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ARTPICST";
const wchar_t APP_URL[] = L"https://github.com/LiebeBlack/Pic";

// Colores premium
const Color BG_COLOR(255, 8, 10, 14);
const Color ACCENT_COLOR(255, 48, 120, 235);
const Color TEXT_COLOR(255, 242, 245, 250);
const Color CARD_BG(255, 16, 18, 23);
const Color CARD_BORDER(255, 80, 80, 80);
const Color BUTTON_NORMAL(255, 55, 55, 55);
const Color BUTTON_HOT(255, 48, 120, 235);
const Color BUTTON_BORDER(255, 40, 40, 40);

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
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken = 0;
};

InstallerState g_state;

// ============================================================================
// Utilidades de sistema de archivos y registro
// ============================================================================

std::wstring GetModulePath() {
    wchar_t path[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return {};
    return path;
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
    // Instalación por usuario (sin necesidad de administrador)
    std::wstring localAppData = GetShellFolder(CSIDL_LOCAL_APPDATA);
    if (!localAppData.empty()) {
        return localAppData + L"\\Programs\\" + APP_NAME;
    }
    std::wstring profile = GetShellFolder(CSIDL_PROFILE);
    if (!profile.empty()) {
        return profile + L"\\" + APP_NAME;
    }
    return L"C:\\" + std::wstring(APP_NAME);
}

bool CopyFileIfExists(const std::wstring& src, const std::wstring& dst) {
    if (src.empty() || dst.empty()) return false;
    if (GetFileAttributesW(src.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    return CopyFileW(src.c_str(), dst.c_str(), FALSE) != FALSE;
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
    wchar_t buffer[512] = {};
    DWORD size = sizeof(buffer);
    const LSTATUS result = RegQueryValueExW(hKey, name.empty() ? nullptr : name.c_str(), nullptr, nullptr,
                                            reinterpret_cast<LPBYTE>(buffer), &size);
    RegCloseKey(hKey);
    if (result != ERROR_SUCCESS) return false;
    outValue = buffer;
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
// Registro de asociaciones de archivo (por usuario, sin administrador)
// ============================================================================

bool RegisterFileAssociations(const std::wstring& exePath) {
    if (exePath.empty()) return false;
    const std::wstring fileType = L"ARTPICST.Image";
    const std::wstring command = L"\"" + exePath + L"\" \"%1\"";
    const std::wstring base = L"Software\\Classes\\" + fileType;
    const std::vector<std::wstring> extensions = {
        L".png", L".jpg", L".jpeg", L".bmp", L".gif", L".tif", L".tiff",
        L".webp", L".ico", L".heic", L".heif", L".avif", L".jfif"
    };

    bool ok = true;
    ok = ok && SetRegStringValue(HKEY_CURRENT_USER, base, L"", L"ARTPICST Image");
    ok = ok && SetRegStringValue(HKEY_CURRENT_USER, base, L"Content Type", L"image/*");
    ok = ok && SetRegStringValue(HKEY_CURRENT_USER, base, L"FriendlyTypeName", L"ARTPICST Image");
    ok = ok && SetRegStringValue(HKEY_CURRENT_USER, base + L"\\DefaultIcon", L"", L"\"" + exePath + L"\",0");
    ok = ok && SetRegStringValue(HKEY_CURRENT_USER, base + L"\\shell\\open\\command", L"", command);
    ok = ok && SetRegStringValue(HKEY_CURRENT_USER, base + L"\\shell\\open", L"MuiVerb", L"Abrir");
    ok = ok && SetRegStringValue(HKEY_CURRENT_USER, base + L"\\shell\\open", L"Icon", L"\"" + exePath + L"\",0");
    ok = ok && SetRegStringValue(HKEY_CURRENT_USER, L"Software\\Classes\\Applications\\artpicst.exe", L"FriendlyAppName", L"ARTPICST");
    ok = ok && SetRegStringValue(HKEY_CURRENT_USER, L"Software\\Classes\\Applications\\artpicst.exe\\shell\\open\\command", L"", command);
    ok = ok && SetRegStringValue(HKEY_CURRENT_USER, L"Software\\Classes\\Applications\\artpicst.exe\\shell\\open", L"MuiVerb", L"Abrir");
    ok = ok && SetRegStringValue(HKEY_CURRENT_USER, L"Software\\Classes\\Applications\\artpicst.exe\\shell\\open", L"Icon", L"\"" + exePath + L"\",0");
    for (const auto& ext : extensions) {
        ok = ok && SetRegStringValue(HKEY_CURRENT_USER, L"Software\\Classes\\" + ext, L"", fileType);
    }
    return ok;
}

void RemoveAssociationIfOurs(const std::wstring& ext) {
    const std::wstring key = L"Software\\Classes\\" + ext;
    std::wstring value;
    if (ReadRegStringValue(HKEY_CURRENT_USER, key, L"", value) && value == L"ARTPICST.Image") {
        RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str());
    }
}

bool WriteUninstallEntry(const std::wstring& installDir) {
    const std::wstring uninstallCmd = L"\"" + installDir + L"\\artpicst_installer.exe\" " + UNINSTALL_SWITCH;
    bool ok = true;
    ok = ok && SetRegStringValue(HKEY_CURRENT_USER, UNINSTALL_REG_KEY, L"DisplayName", APP_NAME);
    ok = ok && SetRegStringValue(HKEY_CURRENT_USER, UNINSTALL_REG_KEY, L"DisplayVersion", APP_VERSION);
    ok = ok && SetRegStringValue(HKEY_CURRENT_USER, UNINSTALL_REG_KEY, L"Publisher", APP_NAME);
    ok = ok && SetRegStringValue(HKEY_CURRENT_USER, UNINSTALL_REG_KEY, L"DisplayIcon", installDir + L"\\artpicst.exe,0");
    ok = ok && SetRegStringValue(HKEY_CURRENT_USER, UNINSTALL_REG_KEY, L"UninstallString", uninstallCmd);
    ok = ok && SetRegStringValue(HKEY_CURRENT_USER, UNINSTALL_REG_KEY, L"InstallLocation", installDir);
    ok = ok && SetRegStringValue(HKEY_CURRENT_USER, UNINSTALL_REG_KEY, L"URLInfoAbout", APP_URL);
    ok = ok && SetRegDwordValue(HKEY_CURRENT_USER, UNINSTALL_REG_KEY, L"NoModify", 1);
    ok = ok && SetRegDwordValue(HKEY_CURRENT_USER, UNINSTALL_REG_KEY, L"NoRepair", 1);
    return ok;
}

// ============================================================================
// Desinstalación
// ============================================================================

void PerformUninstall() {
    const std::wstring installDir = GetModuleFolder();
    const std::wstring selfPath = GetModulePath();

    // Accesos directos
    const std::wstring desktop = GetShellFolder(CSIDL_DESKTOPDIRECTORY);
    const std::wstring programs = GetShellFolder(CSIDL_PROGRAMS);
    if (!desktop.empty()) DeleteFileW((desktop + L"\\ARTPICST.lnk").c_str());
    if (!programs.empty()) {
        const std::wstring menuDir = programs + L"\\ARTPICST";
        DeleteFileW((menuDir + L"\\ARTPICST.lnk").c_str());
        DeleteFileW((menuDir + L"\\Uninstall ARTPICST.lnk").c_str());
        RemoveDirectoryW(menuDir.c_str());
    }

    // Registro
    RegDeleteTreeW(HKEY_CURRENT_USER, UNINSTALL_REG_KEY);
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\ARTPICST.Image");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\Applications\\artpicst.exe");
    const std::vector<std::wstring> extensions = {
        L".png", L".jpg", L".jpeg", L".bmp", L".gif", L".tif", L".tiff",
        L".webp", L".ico", L".heic", L".heif", L".avif", L".jfif"
    };
    for (const auto& ext : extensions) {
        RemoveAssociationIfOurs(ext);
    }

    // Archivos (el propio desinstalador se renombra y elimina al final)
    DeleteFileW((installDir + L"\\artpicst.exe").c_str());
    DeleteFileW((installDir + L"\\artpicst.ico").c_str());
    DeleteFileW((installDir + L"\\README.md").c_str());
    DeleteFileW((installDir + L"\\version.json").c_str());

    // Renombrar el ejecutable en ejecución y programar la limpieza final
    wchar_t tempDir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempDir);
    std::wstring movedSelf = std::wstring(tempDir) + L"artpicst_uninstaller_" + std::to_wstring(GetCurrentProcessId()) + L".exe";
    if (!selfPath.empty()) MoveFileW(selfPath.c_str(), movedSelf.c_str());

    std::wstring cmd = L"/c ping 127.0.0.1 -n 3 >nul & del /f /q \"" + movedSelf + L"\" & rd /s /q \"" + installDir + L"\"";
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) {
        if (pi.hProcess) CloseHandle(pi.hProcess);
        if (pi.hThread) CloseHandle(pi.hThread);
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

// ============================================================================
// Interfaz gráfica
// ============================================================================

void DrawRoundedRect(Graphics& g, const RectF& rect, float radius, const Color& fillColor, const Color& borderColor, float borderWidth = 1.0f) {
    GraphicsPath path;
    float diameter = radius * 2.0f;
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180, 90);
    path.AddArc(rect.X + rect.Width - diameter, rect.Y, diameter, diameter, 270, 90);
    path.AddArc(rect.X + rect.Width - diameter, rect.Y + rect.Height - diameter, diameter, diameter, 0, 90);
    path.AddArc(rect.X, rect.Y + rect.Height - diameter, diameter, diameter, 90, 90);
    path.CloseFigure();
    
    SolidBrush fillBrush(fillColor);
    g.FillPath(&fillBrush, &path);
    
    if (borderWidth > 0) {
        Pen borderPen(borderColor, borderWidth);
        g.DrawPath(&borderPen, &path);
    }
}

void DrawButton(Graphics& g, const RectF& rect, const wchar_t* text, bool isHot, bool isEnabled) {
    Color bgColor = isEnabled ? (isHot ? BUTTON_HOT : BUTTON_NORMAL) : Color(255, 40, 40, 40);
    Color borderColor = isEnabled ? BUTTON_BORDER : Color(255, 60, 60, 60);
    
    DrawRoundedRect(g, rect, 8.0f, bgColor, borderColor);
    
    if (isEnabled) {
        Gdiplus::FontFamily fontFamily(L"Segoe UI");
        Gdiplus::Font font(&fontFamily, 11.0f, FontStyleBold, UnitPoint);
        StringFormat format;
        format.SetAlignment(StringAlignmentCenter);
        format.SetLineAlignment(StringAlignmentCenter);
        
        SolidBrush textBrush(TEXT_COLOR);
        g.DrawString(text, -1, &font, rect, &format, &textBrush);
    }
}

void DrawProgress(Graphics& g, const RectF& rect, int progress) {
    // Fondo
    DrawRoundedRect(g, rect, 6.0f, Color(255, 24, 27, 32), Color(255, 60, 60, 60));
    
    // Progreso
    if (progress > 0) {
        float progressWidth = rect.Width * (progress / 100.0f);
        RectF progressRect(rect.X, rect.Y, progressWidth, rect.Height);
        
        // Clip region for progress
        Region clipRegion(progressRect);
        g.SetClip(&clipRegion);
        
        DrawRoundedRect(g, rect, 6.0f, ACCENT_COLOR, Color(255, 100, 180, 255));
        
        g.ResetClip();
    }
}

void RenderWelcome(Graphics& g, const RECT& client) {
    float centerX = client.right / 2.0f;
    float centerY = client.bottom / 2.0f;
    
    // Logo/Title
    Gdiplus::FontFamily titleFamily(L"Segoe UI");
    Gdiplus::Font titleFont(&titleFamily, 32.0f, FontStyleBold, UnitPoint);
    Gdiplus::Font subtitleFont(&titleFamily, 14.0f, FontStyleRegular, UnitPoint);
    
    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);
    
    SolidBrush textBrush(TEXT_COLOR);
    
    RectF titleRect(0.0f, centerY - 120.0f, static_cast<REAL>(client.right), 50.0f);
    g.DrawString(APP_NAME, -1, &titleFont, titleRect, &format, &textBrush);
    
    RectF subtitleRect(0.0f, centerY - 70.0f, static_cast<REAL>(client.right), 30.0f);
    g.DrawString(L"Visor de imágenes premium con interfaz acrílica moderna", -1, &subtitleFont, subtitleRect, &format, &textBrush);
    
    // Feature card
    RectF cardRect(centerX - 200.0f, centerY - 20.0f, 400.0f, 200.0f);
    DrawRoundedRect(g, cardRect, 16.0f, CARD_BG, CARD_BORDER);
    
    Gdiplus::Font featureFont(&titleFamily, 11.0f, FontStyleRegular, UnitPoint);
    SolidBrush featureBrush(Color(255, 200, 210, 220));
    
    const wchar_t* features[] = {
        L"• Interfaz Glassmorphism/Acrílica premium",
        L"• Soporte para más de 30 formatos de imagen",
        L"• Ultra-Claridad HDR y efectos avanzados",
        L"• Navegación fluida con zoom y pan",
        L"• Rendimiento ultra-ligero y bajo consumo de recursos"
    };
    
    float featureY = cardRect.Y + 30.0f;
    for (const wchar_t* feature : features) {
        RectF featureRect(cardRect.X + 20.0f, featureY, cardRect.Width - 40.0f, 25.0f);
        g.DrawString(feature, -1, &featureFont, featureRect, &format, &featureBrush);
        featureY += 30.0f;
    }
}

void RenderLicense(Graphics& g, const RECT& client) {
    float centerX = static_cast<float>(client.right) / 2.0f;
    float centerY = static_cast<float>(client.bottom) / 2.0f;
    
    Gdiplus::FontFamily titleFamily(L"Segoe UI");
    Gdiplus::Font titleFont(&titleFamily, 24.0f, FontStyleBold, UnitPoint);
    Gdiplus::Font textFont(&titleFamily, 11.0f, FontStyleRegular, UnitPoint);
    
    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);
    
    SolidBrush textBrush(TEXT_COLOR);
    
    RectF titleRect(0.0f, centerY - 150.0f, static_cast<REAL>(client.right), 40.0f);
    g.DrawString(L"Acuerdo de Licencia", -1, &titleFont, titleRect, &format, &textBrush);
    
    // License text box
    RectF licenseBox(centerX - 250, centerY - 100, 500, 200);
    DrawRoundedRect(g, licenseBox, 12.0f, Color(255, 12, 14, 18), Color(255, 60, 60, 60));
    
    const wchar_t* licenseText = L"ARTPICST - Visor de Imágenes Premium\n\n"
        L"Este software es proporcionado tal cual, sin garantía de ningún tipo.\n"
        L"El usuario es responsable de su uso y consecuencias.\n\n"
        L"Características principales:\n"
        L"- Visualización de imágenes de alta calidad\n"
        L"- Interfaz moderna con efectos acrílicos\n"
        L"- Soporte para múltiples formatos\n"
        L"- Rendimiento ultra-ligero y optimizado\n\n"
        L"Al continuar con la instalación, aceptas los términos de uso.";
    
    StringFormat textFormat;
    textFormat.SetAlignment(StringAlignmentNear);
    textFormat.SetLineAlignment(StringAlignmentNear);
    
    RectF textRect(licenseBox.X + 20, licenseBox.Y + 20, licenseBox.Width - 40, licenseBox.Height - 40);
    SolidBrush licenseBrush(Color(255, 180, 190, 200));
    g.DrawString(licenseText, -1, &textFont, textRect, &textFormat, &licenseBrush);
}

void RenderInstall(Graphics& g, const RECT& client) {
    float centerX = static_cast<float>(client.right) / 2.0f;
    float centerY = static_cast<float>(client.bottom) / 2.0f;
    
    Gdiplus::FontFamily titleFamily(L"Segoe UI");
    Gdiplus::Font titleFont(&titleFamily, 24.0f, FontStyleBold, UnitPoint);
    Gdiplus::Font textFont(&titleFamily, 11.0f, FontStyleRegular, UnitPoint);
    
    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);
    
    SolidBrush textBrush(TEXT_COLOR);
    
    RectF titleRect(0.0f, centerY - 150.0f, static_cast<REAL>(client.right), 40.0f);
    g.DrawString(L"Instalando ARTPICST", -1, &titleFont, titleRect, &format, &textBrush);
    
    // Progress bar
    RectF progressRect(centerX - 200.0f, centerY - 50.0f, 400.0f, 24.0f);
    DrawProgress(g, progressRect, g_state.installProgress);
    
    // Status text
    wchar_t statusText[512];
    swprintf_s(statusText, L"%s\n\nProgreso: %d%%", g_state.installStatus.c_str(), g_state.installProgress);
    
    RectF statusRect(0.0f, centerY - 10.0f, static_cast<REAL>(client.right), 50.0f);
    g.DrawString(statusText, -1, &textFont, statusRect, &format, &textBrush);
}

void RenderComplete(Graphics& g, const RECT& client) {
    float centerY = static_cast<float>(client.bottom) / 2.0f;
    
    Gdiplus::FontFamily titleFamily(L"Segoe UI");
    Gdiplus::Font titleFont(&titleFamily, 28.0f, FontStyleBold, UnitPoint);
    Gdiplus::Font textFont(&titleFamily, 12.0f, FontStyleRegular, UnitPoint);
    
    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);
    
    SolidBrush textBrush(TEXT_COLOR);
    
    if (g_state.installSucceeded) {
        RectF titleRect(0.0f, centerY - 100.0f, static_cast<REAL>(client.right), 50.0f);
        g.DrawString(L"¡Instalación Completada!", -1, &titleFont, titleRect, &format, &textBrush);
        
        const wchar_t* completeText = L"ARTPICST se ha instalado correctamente en tu sistema.\n\n"
            L"Ubicación de instalación:\n"
            L"%s\n\n"
            L"Puedes iniciar la aplicación desde el menú de inicio\n"
            L"o haciendo clic en el botón de abajo.";
        wchar_t buffer[1024];
        swprintf_s(buffer, completeText, g_state.installPath.c_str());
        
        RectF textRect(0.0f, centerY - 30.0f, static_cast<REAL>(client.right), 100.0f);
        g.DrawString(buffer, -1, &textFont, textRect, &format, &textBrush);
    } else {
        RectF titleRect(0.0f, centerY - 100.0f, static_cast<REAL>(client.right), 50.0f);
        g.DrawString(L"Error en la Instalación", -1, &titleFont, titleRect, &format, &textBrush);
        
        const wchar_t* errorText = L"No se pudo completar la instalación.\n\n"
            L"Asegúrate de que el instalador se encuentre junto a\n"
            L"artpicst.exe en la misma carpeta e inténtalo de nuevo.";
        
        RectF textRect(0.0f, centerY - 30.0f, static_cast<REAL>(client.right), 60.0f);
        g.DrawString(errorText, -1, &textFont, textRect, &format, &textBrush);
    }
}

void RenderWindow(Graphics& g, const RECT& client) {
    // Background
    SolidBrush bgBrush(BG_COLOR);
    g.FillRectangle(&bgBrush, 0, 0, client.right, client.bottom);
    
    // Header line
    Pen headerPen(ACCENT_COLOR, 2.0f);
    g.DrawLine(&headerPen, 0.0f, 0.0f, static_cast<REAL>(client.right), 0.0f);
    
    // Render current step
    switch (g_state.currentStep) {
        case InstallStep::Welcome:
            RenderWelcome(g, client);
            break;
        case InstallStep::License:
            RenderLicense(g, client);
            break;
        case InstallStep::Install:
            RenderInstall(g, client);
            break;
        case InstallStep::Complete:
            RenderComplete(g, client);
            break;
    }
    
    // Navigation buttons
    float buttonY = static_cast<float>(client.bottom) - 60.0f;
    float buttonWidth = 120.0f;
    float buttonHeight = 36.0f;
    
    RectF backButton(20.0f, buttonY, buttonWidth, buttonHeight);
    RectF nextButton(static_cast<REAL>(client.right) - buttonWidth - 20.0f, buttonY, buttonWidth, buttonHeight);
    
    const wchar_t* backText = L"← Atrás";
    const wchar_t* nextText = L"Siguiente →";
    
    switch (g_state.currentStep) {
        case InstallStep::Welcome:
            DrawButton(g, nextButton, nextText, false, true);
            break;
        case InstallStep::License:
            DrawButton(g, backButton, backText, false, true);
            DrawButton(g, nextButton, L"Aceptar", false, true);
            break;
        case InstallStep::Install:
            // No buttons during installation
            break;
        case InstallStep::Complete:
            DrawButton(g, RectF(static_cast<REAL>(client.right) - buttonWidth - 20.0f, buttonY, buttonWidth, buttonHeight),
                      g_state.installSucceeded ? L"Iniciar App" : L"Cerrar", false, true);
            break;
    }
    
    // Cancel button (top right)
    RectF cancelRect(static_cast<REAL>(client.right) - 80.0f, 10.0f, 60.0f, 24.0f);
    DrawButton(g, cancelRect, L"Cancelar", false, g_state.currentStep != InstallStep::Install);
}

// ============================================================================
// Instalación real
// ============================================================================

void PerformInstallation() {
    g_state.isInstalling = true;
    g_state.currentStep = InstallStep::Install;
    g_state.installProgress = 0;
    InvalidateRect(g_state.hwnd, nullptr, FALSE);

    const std::wstring srcDir = GetModuleFolder();
    const std::wstring dst = g_state.installPath;
    const std::wstring selfPath = GetModulePath();

    auto step = [](int progress, const wchar_t* status) {
        g_state.installProgress = progress;
        g_state.installStatus = status;
        InvalidateRect(g_state.hwnd, nullptr, FALSE);
        UpdateWindow(g_state.hwnd);
        Sleep(60);
    };

    bool ok = true;

    step(5, L"Preparando el directorio de instalación...");
    if (dst.empty()) {
        ok = false;
    } else if (!CreateDirectoryW(dst.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        ok = false;
    }

    if (ok) {
        step(20, L"Copiando el programa principal...");
        ok = CopyFileIfExists(srcDir + L"\\artpicst.exe", dst + L"\\artpicst.exe");
    }

    if (ok) {
        step(35, L"Copiando recursos y documentación...");
        CopyFileIfExists(srcDir + L"\\artpicst.ico", dst + L"\\artpicst.ico");
        CopyFileIfExists(srcDir + L"\\README.md", dst + L"\\README.md");
        CopyFileIfExists(srcDir + L"\\version.json", dst + L"\\version.json");
        ok = !selfPath.empty() && CopyFileW(selfPath.c_str(), (dst + L"\\artpicst_installer.exe").c_str(), FALSE) != FALSE;
    }

    if (ok) {
        step(55, L"Creando accesos directos...");
        const std::wstring desktop = GetShellFolder(CSIDL_DESKTOPDIRECTORY);
        const std::wstring programs = GetShellFolder(CSIDL_PROGRAMS);
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
        step(70, L"Registrando asociaciones de archivo...");
        ok = !g_state.registerFileAssociations || RegisterFileAssociations(dst + L"\\artpicst.exe");
    }

    if (ok) {
        step(85, L"Registrando el desinstalador...");
        ok = WriteUninstallEntry(dst);
    }

    if (ok) {
        step(100, L"Instalación completada.");
    } else {
        step(100, L"Error: no se pudo completar la instalación.");
    }

    g_state.isInstalling = false;
    g_state.installSucceeded = ok;
    g_state.currentStep = InstallStep::Complete;
    InvalidateRect(g_state.hwnd, nullptr, FALSE);
    UpdateWindow(g_state.hwnd);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_state.hwnd = hwnd;
            g_state.installPath = GetDefaultInstallPath();
            
            // Enable dark title bar
            BOOL darkMode = TRUE;
            DwmSetWindowAttribute(hwnd, 20, &darkMode, sizeof(BOOL));
            DwmSetWindowAttribute(hwnd, 19, &darkMode, sizeof(BOOL));
            
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            Graphics graphics(hdc);
            graphics.SetCompositingQuality(CompositingQualityHighQuality);
            graphics.SetSmoothingMode(SmoothingModeHighQuality);
            graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
            
            RECT client;
            GetClientRect(hwnd, &client);
            RenderWindow(graphics, client);
            
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return TRUE;
        case WM_DESTROY:
            GdiplusShutdown(g_state.gdiplusToken);
            PostQuitMessage(0);
            return 0;
        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            
            RECT client;
            GetClientRect(hwnd, &client);
            
            float buttonY = static_cast<float>(client.bottom) - 60.0f;
            float buttonWidth = 120.0f;
            float buttonHeight = 36.0f;
            
            // Next button
            RectF nextButton(static_cast<REAL>(client.right) - buttonWidth - 20.0f, buttonY, buttonWidth, buttonHeight);
            if (x >= nextButton.X && x <= nextButton.X + nextButton.Width &&
                y >= nextButton.Y && y <= nextButton.Y + nextButton.Height) {
                
                switch (g_state.currentStep) {
                    case InstallStep::Welcome:
                        g_state.currentStep = InstallStep::License;
                        break;
                    case InstallStep::License:
                        PerformInstallation();
                        break;
                    case InstallStep::Complete:
                        if (g_state.installSucceeded) {
                            // Launch application
                            ShellExecuteW(nullptr, L"open", (g_state.installPath + L"\\artpicst.exe").c_str(),
                                          nullptr, g_state.installPath.c_str(), SW_SHOWNORMAL);
                        }
                        PostMessage(hwnd, WM_CLOSE, 0, 0);
                        break;
                    default:
                        break;
                }
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            
            // Back button
            RectF backButton(20.0f, buttonY, buttonWidth, buttonHeight);
            if (x >= backButton.X && x <= backButton.X + backButton.Width &&
                y >= backButton.Y && y <= backButton.Y + backButton.Height) {
                
                if (g_state.currentStep == InstallStep::License) {
                    g_state.currentStep = InstallStep::Welcome;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            
            // Cancel button
            RectF cancelRect(static_cast<REAL>(client.right) - 80.0f, 10.0f, 60.0f, 24.0f);
            if (x >= cancelRect.X && x <= cancelRect.X + cancelRect.Width &&
                y >= cancelRect.Y && y <= cancelRect.Y + cancelRect.Height) {
                
                if (!g_state.isInstalling) {
                    PostMessage(hwnd, WM_CLOSE, 0, 0);
                }
            }
            
            return 0;
        }
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    (void)hPrevInstance;

    // Detectar modo desinstalación (--uninstall)
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool uninstallRequested = false;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (wcscmp(argv[i], UNINSTALL_SWITCH) == 0) {
                uninstallRequested = true;
                break;
            }
        }
        LocalFree(argv);
    }

    if (uninstallRequested) {
        const int answer = MessageBoxW(nullptr,
            L"¿Desea desinstalar ARTPICST?\n\nSe eliminarán los archivos, accesos directos y asociaciones de archivo.",
            L"Desinstalar ARTPICST", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        if (answer == IDYES) {
            PerformUninstall();
            MessageBoxW(nullptr, L"ARTPICST ha sido desinstalado correctamente.",
                        L"Desinstalación completada", MB_OK | MB_ICONINFORMATION);
        }
        return 0;
    }

    g_state.hInstance = hInstance;
    (void)pCmdLine;

    // Inicializar COM (necesario para crear accesos directos)
    HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comOk = SUCCEEDED(comHr);

    // Initialize GDI+
    GdiplusStartup(&g_state.gdiplusToken, &g_state.gdiplusStartupInput, nullptr);

    // Register window class
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    
    RegisterClassExW(&wc);
    
    // Create window
    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        CLASS_NAME,
        L"Instalador de ARTPICST",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        600, 500,
        nullptr, nullptr, hInstance, nullptr
    );
    
    if (!hwnd) {
        GdiplusShutdown(g_state.gdiplusToken);
        if (comOk) CoUninitialize();
        return 1;
    }
    
    // Center window
    RECT rect;
    GetWindowRect(hwnd, &rect);
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(hwnd, nullptr, (screenWidth - rect.right) / 2, (screenHeight - rect.bottom) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    GdiplusShutdown(g_state.gdiplusToken);
    if (comOk) CoUninitialize();
    return static_cast<int>(msg.wParam);
}