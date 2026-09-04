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

// ============================================================================
// Paleta moderna (tema oscuro refinado con acentos degradados azul-violeta)
// ============================================================================
const Color COL_TEXT(255, 238, 242, 248);          // Texto principal
const Color COL_TEXT_SOFT(255, 168, 178, 194);     // Texto secundario
const Color COL_TEXT_DIM(255, 116, 126, 146);      // Texto terciario / etiquetas
const Color COL_PANEL(255, 20, 25, 35);            // Tarjetas / paneles
const Color COL_PANEL_BORDER(255, 39, 49, 66);     // Borde de tarjetas
const Color COL_BTN_GHOST(255, 28, 35, 48);        // Botón secundario
const Color COL_BTN_GHOST_HOT(255, 42, 53, 72);
const Color COL_BTN_GHOST_BORDER(255, 54, 66, 88);
const Color COL_BTN_GHOST_BORDER_HOT(255, 112, 140, 185);
const Color COL_ACCENT_A(255, 47, 124, 246);       // Degradado: azul
const Color COL_ACCENT_B(255, 130, 92, 246);       // Degradado: violeta
const Color COL_SUCCESS(255, 52, 199, 105);
const Color COL_ERROR(255, 229, 72, 77);
const Color COL_DISABLED_TEXT(255, 112, 120, 136);

// Tamaño de diseño (unidades lógicas 96 DPI); la ventana se escala por g_scale
const float DESIGN_W = 640.0f;
const float DESIGN_H = 540.0f;
const float MIN_DESIGN_W = 560.0f;
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
    const float margin = 48.0f;
    const float buttonH = 42.0f;
    const float buttonY = H - buttonH - 16.0f;

    // Botón primario (derecha) y secundario Atrás (izquierda)
    r.next = RectF(W - margin - 150.0f, buttonY, 150.0f, buttonH);
    r.back = RectF(margin, buttonY, 118.0f, buttonH);

    // Cancelar discreto (arriba a la derecha)
    r.cancel = RectF(W - margin - 84.0f, 14.0f, 84.0f, 26.0f);

    // Filas de opciones de la página de licencia
    if (g_state.currentStep == InstallStep::License) {
        const float rowX = 56.0f;
        const float rowW = W - rowX * 2.0f;
        const float rowH = 38.0f;
        const float gap = 8.0f;
        float y = 306.0f;
        for (int i = 0; i < 3; ++i) {
            r.rows[i] = RectF(rowX, y, rowW, rowH);
            y += rowH + gap;
        }
        r.rowCount = 3;
    }
    return r;
}

int HoverZoneAt(const LayoutRects& r, float lx, float ly) {
    if (g_state.currentStep == InstallStep::Install) return HOVER_NONE;
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

// Crea todos los directorios intermedios de una ruta ("C:\A\B\C" -> C, C\A, ...)
bool CreateDirectoryTree(const std::wstring& path) {
    if (path.empty()) return false;
    std::wstring current;
    size_t start = 0;
    if (path.size() >= 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) {
        current = path.substr(0, 3); // raíz "X:\"
        start = 3;
    }
    for (size_t i = start; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == L'\\' || path[i] == L'/') {
            if (i > start) {
                current += path.substr(start, i - start);
                if (!CreateDirectoryW(current.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
                    return false;
                }
                current += L'\\';
            }
            start = i + 1; // salta el separador (o segmentos vacíos)
        }
    }
    return true;
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
    if (CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) {
        if (pi.hProcess) CloseHandle(pi.hProcess);
        if (pi.hThread) CloseHandle(pi.hThread);
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
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
    FillRound(g, rc, 6.0f, checked ? COL_ACCENT_A : Color(255, 16, 20, 29));
    if (checked) {
        // Degradado sutil sobre la casilla activa
        FillRoundGradient(g, rc, 6.0f, COL_ACCENT_A, COL_ACCENT_B);
        DrawCheckMark(g, rc.X + rc.Width * 0.22f, rc.Y + rc.Height * 0.52f,
                      rc.X + rc.Width * 0.42f, rc.Y + rc.Height * 0.72f,
                      rc.X + rc.Width * 0.80f, rc.Y + rc.Height * 0.28f,
                      2.0f, Color(255, 255, 255, 255));
    } else {
        StrokeRound(g, rc, 6.0f, Color(255, 66, 80, 102), 1.0f);
    }
}

// ============================================================================
// Textos y fuentes reutilizables (UnitPixel: escalan con la transformación DPI)
// ============================================================================

struct Fonts {
    FontFamily family;
    Font fLogo;      // 30 px
    Font fTitle;     // 26 px
    Font fHeading;   // 20 px
    Font fBody;      // 14 px
    Font fSmall;     // 13 px
    Font fLabel;     // 11 px, negrita (etiquetas)
    Font fTiny;      // 10.5 px
    Fonts()
        : family(L"Segoe UI"),
          fLogo(&family, 30.0f, FontStyleBold, UnitPixel),
          fTitle(&family, 26.0f, FontStyleBold, UnitPixel),
          fHeading(&family, 20.0f, FontStyleBold, UnitPixel),
          fBody(&family, 14.0f, FontStyleRegular, UnitPixel),
          fSmall(&family, 13.0f, FontStyleRegular, UnitPixel),
          fLabel(&family, 11.0f, FontStyleBold, UnitPixel),
          fTiny(&family, 10.5f, FontStyleRegular, UnitPixel) {}
};

// Logo: baldosa redondeada con degradado de marca y la letra inicial
static void DrawLogo(Graphics& g, const Fonts& fonts, float cx, float cy, float size) {
    const RectF tile(cx - size * 0.5f, cy - size * 0.5f, size, size);
    FillRoundGradient(g, tile, size * 0.24f, COL_ACCENT_A, COL_ACCENT_B);
    // Brillo superior sutil (efecto cristal)
    const RectF shine(tile.X, tile.Y, tile.Width, tile.Height * 0.5f);
    FillRound(g, shine, size * 0.24f, Color(30, 255, 255, 255));
    DrawTextIn(g, L"A", tile, fonts.fLogo, Color(255, 255, 255, 255), true, true);
}

// Fondo general: degradado vertical profundo + barra de acento superior
static void DrawChrome(Graphics& g, const Fonts&, float W, float H) {
    LinearGradientBrush bg(RectF(0.0f, 0.0f, W, H), Color(255, 15, 19, 27), Color(255, 8, 10, 15), 90.0f);
    g.FillRectangle(&bg, 0.0f, 0.0f, W, H);

    // Barra superior degradada (firma de la marca)
    const RectF topBar(0.0f, 0.0f, W, 3.0f);
    LinearGradientBrush accent(topBar, COL_ACCENT_A, COL_ACCENT_B, 0.0f);
    g.FillRectangle(&accent, topBar);
}

// Botón primario con degradado
static void DrawPrimaryButton(Graphics& g, const Fonts& fonts, const RectF& rc, const wchar_t* text, bool hot) {
    FillRoundGradient(g, rc, 9.0f, COL_ACCENT_A, COL_ACCENT_B);
    if (hot) {
        FillRound(g, rc, 9.0f, Color(24, 255, 255, 255)); // realce hover
    }
    DrawTextIn(g, text, rc, fonts.fSmall, Color(255, 255, 255, 255), true, true);
}

// Botón secundario discreto (Atrás / Cerrar / Reintentar / Cancelar)
static void DrawGhostButton(Graphics& g, const Fonts& fonts, const RectF& rc, const wchar_t* text, bool hot) {
    FillRound(g, rc, 9.0f, hot ? COL_BTN_GHOST_HOT : COL_BTN_GHOST);
    StrokeRound(g, rc, 9.0f, hot ? COL_BTN_GHOST_BORDER_HOT : COL_BTN_GHOST_BORDER, 1.0f);
    DrawTextIn(g, text, rc, fonts.fSmall, COL_TEXT, true, true);
}

// ============================================================================
// Pantallas del asistente
// ============================================================================

void RenderWelcome(Graphics& g, const Fonts& fonts, float W, float H) {
    const float cx = W * 0.5f;

    DrawLogo(g, fonts, cx, 88.0f, 64.0f);

    RectF nameRect(cx - 200.0f, 128.0f, 400.0f, 44.0f);
    DrawTextIn(g, APP_NAME, nameRect, fonts.fTitle, COL_TEXT, true, false);

    RectF tagRect(cx - 260.0f, 176.0f, 520.0f, 26.0f);
    DrawTextIn(g, L"Visor de imágenes premium para Windows — rápido, ligero y moderno",
               tagRect, fonts.fSmall, COL_TEXT_SOFT, true, true);

    // Tarjeta de características
    const float cardY = 222.0f;
    const RectF card(cx - 256.0f, cardY, 512.0f, 176.0f);
    FillRound(g, card, 16.0f, COL_PANEL);
    StrokeRound(g, card, 16.0f, COL_PANEL_BORDER, 1.0f);

    struct Feature { const wchar_t* text; };
    const Feature features[] = {
        { L"Interfaz moderna: modo oscuro/claro y acabados acrílicos" },
        { L"Más de 30 formatos: PNG, WebP, HEIC, AVIF, GIF, RAW y más" },
        { L"Zoom fluido, píxel perfecto al 100% y Ultra-Claridad HDR" },
        { L"Ultra-ligero: baja memoria y CPU ~0% en reposo" }
    };

    float fy = cardY + 22.0f;
    for (int i = 0; i < 4; ++i) {
        // Punto de acento degradado
        const float dotR = 4.0f;
        const float dotY = fy + 9.0f;
        FillRoundGradient(g, RectF(card.X + 24.0f, dotY - dotR, dotR * 2.0f, dotR * 2.0f), dotR, COL_ACCENT_A, COL_ACCENT_B);
        RectF featureRect(card.X + 42.0f, fy - 2.0f, card.Width - 62.0f, 24.0f);
        DrawTextIn(g, features[i].text, featureRect, fonts.fSmall, COL_TEXT_SOFT, false, true);
        fy += 33.0f;
    }

    RectF hint(cx - 200.0f, H - 92.0f, 400.0f, 20.0f);
    DrawTextIn(g, L"Se instalará para tu usuario, sin permisos de administrador.",
               hint, fonts.fTiny, COL_TEXT_DIM, true, true);
}

void RenderLicense(Graphics& g, const Fonts& fonts, float W, float H, const LayoutRects& layout) {
    const float cx = W * 0.5f;

    RectF titleRect(cx - 250.0f, 40.0f, 500.0f, 42.0f);
    DrawTextIn(g, L"Licencia y opciones", titleRect, fonts.fHeading, COL_TEXT, true, true);

    // Caja del acuerdo
    const RectF box(cx - 264.0f, 92.0f, 528.0f, 184.0f);
    FillRound(g, box, 14.0f, Color(255, 17, 21, 30));
    StrokeRound(g, box, 14.0f, COL_PANEL_BORDER, 1.0f);

    RectF boxTitle(box.X + 20.0f, box.Y + 12.0f, box.Width - 40.0f, 18.0f);
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
    const float textW = box.Width - 40.0f;
    const float textH = box.Height - 42.0f;
    const RectF textArea(box.X + 20.0f, box.Y + 36.0f, textW, textH);
    float fontSize = 12.0f;
    for (int attempt = 0; attempt < 8; ++attempt) {
        Font probe(&fonts.family, fontSize, FontStyleRegular, UnitPixel);
        RectF measured;
        StringFormat probeFormat;
        probeFormat.SetFormatFlags(StringFormatFlagsLineLimit);
        g.MeasureString(licenseText, -1, &probe, RectF(0.0f, 0.0f, textW, 2000.0f), &probeFormat, &measured);
        if (measured.Height <= textH || fontSize <= 8.5f) break;
        fontSize -= 0.5f;
    }
    Font licenseFont(&fonts.family, fontSize, FontStyleRegular, UnitPixel);
    StringFormat textFormat;
    textFormat.SetAlignment(StringAlignmentNear);
    textFormat.SetLineAlignment(StringAlignmentNear);
    textFormat.SetFormatFlags(StringFormatFlagsLineLimit);
    SolidBrush licenseBrush(Color(255, 176, 186, 202));
    g.DrawString(licenseText, -1, &licenseFont, textArea, &textFormat, &licenseBrush);

    // Opciones de instalación
    RectF optTitle(cx - 264.0f, 286.0f, 528.0f, 16.0f);
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
        FillRound(g, row, 10.0f, hot ? Color(255, 26, 33, 46) : Color(255, 22, 27, 38));
        StrokeRound(g, row, 10.0f, hot ? COL_BTN_GHOST_BORDER_HOT : COL_PANEL_BORDER, 1.0f);

        const RectF boxRect(row.X + 14.0f, row.Y + (row.Height - 20.0f) * 0.5f, 20.0f, 20.0f);
        DrawCheckBox(g, boxRect, *rows[i].value);

        RectF labelRect(row.X + 46.0f, row.Y, row.Width - 58.0f, row.Height);
        DrawTextIn(g, rows[i].label, labelRect, fonts.fSmall, COL_TEXT, false, true);
    }
}

void RenderInstall(Graphics& g, const Fonts& fonts, float W, float H) {
    const float cx = W * 0.5f;

    DrawLogo(g, fonts, cx, 120.0f, 56.0f);

    RectF titleRect(cx - 260.0f, 164.0f, 520.0f, 40.0f);
    DrawTextIn(g, L"Instalando ARTPICST", titleRect, fonts.fHeading, COL_TEXT, true, true);

    RectF subRect(cx - 260.0f, 206.0f, 520.0f, 22.0f);
    DrawTextIn(g, L"Se está instalando en tu equipo. No cierres esta ventana.",
               subRect, fonts.fSmall, COL_TEXT_SOFT, true, true);

    // Barra de progreso
    const float barY = 252.0f;
    const RectF track(cx - 220.0f, barY, 440.0f, 12.0f);
    FillRound(g, track, 6.0f, Color(255, 26, 31, 42));
    StrokeRound(g, track, 6.0f, Color(255, 43, 53, 70), 1.0f);

    if (g_state.installProgress > 0) {
        const float fillW = track.Width * (g_state.installProgress / 100.0f);
        if (fillW > 1.0f) {
            const RectF fill(track.X, track.Y, fillW, track.Height);
            Region clip(fill);
            g.SetClip(&clip);
            FillRoundGradient(g, track, 6.0f, COL_ACCENT_A, COL_ACCENT_B);
            g.ResetClip();
        }
    }

    // Estado y porcentaje
    wchar_t statusText[512];
    swprintf_s(statusText, 512, L"%s", g_state.installStatus.c_str());
    RectF statusRect(cx - 280.0f, barY + 28.0f, 560.0f, 24.0f);
    DrawTextIn(g, statusText, statusRect, fonts.fSmall, COL_TEXT, true, true);

    wchar_t percentText[32];
    swprintf_s(percentText, 32, L"%d%%", g_state.installProgress);
    RectF pctRect(cx - 280.0f, barY + 52.0f, 560.0f, 20.0f);
    DrawTextIn(g, percentText, pctRect, fonts.fLabel, COL_TEXT_SOFT, true, true);

    // Ruta de destino
    std::wstring dest = L"Destino: " + g_state.installPath;
    RectF destRect(cx - 300.0f, H - 108.0f, 600.0f, 20.0f);
    DrawTextIn(g, dest.c_str(), destRect, fonts.fTiny, COL_TEXT_DIM, true, true, StringTrimmingEllipsisCharacter, true);
}

void RenderComplete(Graphics& g, const Fonts& fonts, float W, float H) {
    const float cx = W * 0.5f;

    if (g_state.installSucceeded) {
        // Anillo verde con check
        const float r = 38.0f;
        const float cy = 132.0f;
        const RectF ring(cx - r, cy - r, r * 2.0f, r * 2.0f);
        GraphicsPath ringPath;
        RoundPath(ringPath, ring, r);
        SolidBrush ringBrush(COL_SUCCESS);
        g.FillPath(&ringBrush, &ringPath);
        DrawCheckMark(g, cx - 16.0f, cy + 1.0f, cx - 5.0f, cy + 12.0f, cx + 17.0f, cy - 13.0f, 4.5f, Color(255, 255, 255, 255));

        RectF titleRect(cx - 260.0f, 200.0f, 520.0f, 42.0f);
        DrawTextIn(g, L"Instalación completada", titleRect, fonts.fTitle, COL_TEXT, true, true);

        RectF subRect(cx - 260.0f, 246.0f, 520.0f, 22.0f);
        DrawTextIn(g, L"Gracias por elegir ARTPICST.", subRect, fonts.fSmall, COL_TEXT_SOFT, true, true);

        // Tarjeta con la ruta
        const RectF card(cx - 230.0f, 288.0f, 460.0f, 96.0f);
        FillRound(g, card, 14.0f, COL_PANEL);
        StrokeRound(g, card, 14.0f, COL_PANEL_BORDER, 1.0f);

        RectF cardLabel(card.X + 20.0f, card.Y + 14.0f, card.Width - 40.0f, 16.0f);
        DrawTextIn(g, L"UBICACIÓN DE INSTALACIÓN", cardLabel, fonts.fLabel, COL_TEXT_DIM, false, true);

        RectF cardPath(card.X + 20.0f, card.Y + 38.0f, card.Width - 40.0f, 24.0f);
        DrawTextIn(g, g_state.installPath.c_str(), cardPath, fonts.fSmall, COL_TEXT, false, true,
                   StringTrimmingEllipsisCharacter, true);

        RectF cardVer(card.X + 20.0f, card.Y + 66.0f, card.Width - 40.0f, 16.0f);
        std::wstring ver = std::wstring(L"Versión ") + APP_VERSION + L"  ·  Instalación por usuario";
        DrawTextIn(g, ver.c_str(), cardVer, fonts.fTiny, COL_TEXT_SOFT, false, true);
    } else {
        // Anillo rojo con signo de exclamación
        const float r = 38.0f;
        const float cy = 132.0f;
        const RectF ring(cx - r, cy - r, r * 2.0f, r * 2.0f);
        GraphicsPath ringPath;
        RoundPath(ringPath, ring, r);
        SolidBrush ringBrush(COL_ERROR);
        g.FillPath(&ringBrush, &ringPath);
        Font bangFont(&fonts.family, 42.0f, FontStyleBold, UnitPixel);
        DrawTextIn(g, L"!", ring, bangFont, Color(255, 255, 255, 255), true, true);

        RectF titleRect(cx - 260.0f, 196.0f, 520.0f, 42.0f);
        DrawTextIn(g, L"No se pudo completar la instalación", titleRect, fonts.fTitle, COL_TEXT, true, true);

        RectF msgRect(cx - 260.0f, 246.0f, 520.0f, 66.0f);
        DrawTextIn(g, L"Coloca el instalador junto a artpicst.exe, artpicst.ico y version.json\n"
                       L"en la misma carpeta y vuelve a intentarlo.",
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
            RenderComplete(g, fonts, W, H);
            break;
    }

    // Cancelar (arriba a la derecha); nunca durante la instalación
    if (g_state.currentStep != InstallStep::Install) {
        DrawGhostButton(g, fonts, layout.cancel, L"Cancelar",
                        g_state.hoverZone == HOVER_CANCEL && g_state.currentStep != InstallStep::Install);
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
        g_state.installStatus = status;
        InvalidateRect(g_state.hwnd, nullptr, FALSE);
        UpdateWindow(g_state.hwnd);
        Sleep(60);
    };

    bool ok = true;

    step(5, L"Preparando el directorio de instalación...");
    if (dst.empty()) {
        ok = false;
    } else if (!CreateDirectoryTree(dst)) {
        ok = false;
    }

    if (ok) {
        step(18, L"Comprobando archivos de la aplicación...");
        if (GetFileAttributesW((srcDir + L"\\artpicst.exe").c_str()) == INVALID_FILE_ATTRIBUTES) {
            ok = false;
        }
    }

    if (ok) {
        step(32, L"Copiando el programa principal...");
        ok = CopyFileIfExists(srcDir + L"\\artpicst.exe", dst + L"\\artpicst.exe");
    }

    if (ok) {
        step(48, L"Copiando recursos y documentación...");
        CopyFileIfExists(srcDir + L"\\artpicst.ico", dst + L"\\artpicst.ico");
        CopyFileIfExists(srcDir + L"\\README.md", dst + L"\\README.md");
        CopyFileIfExists(srcDir + L"\\version.json", dst + L"\\version.json");
        ok = !selfPath.empty() && CopyFileW(selfPath.c_str(), (dst + L"\\artpicst_installer.exe").c_str(), FALSE) != FALSE;
    }

    if (ok) {
        step(62, L"Creando accesos directos...");
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
        step(76, L"Registrando asociaciones de archivo...");
        ok = !g_state.registerFileAssociations || RegisterFileAssociations(dst + L"\\artpicst.exe");
    }

    if (ok) {
        step(88, L"Registrando el desinstalador...");
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
                ShellExecuteW(nullptr, L"open", (g_state.installPath + L"\\artpicst.exe").c_str(),
                              nullptr, g_state.installPath.c_str(), SW_SHOWNORMAL);
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
    InvalidateRect(g_state.hwnd, nullptr, FALSE);
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
                g_state.hoverZone = zone;
                InvalidateRect(hwnd, nullptr, FALSE);
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

            // Botón principal / Atrás / Cancelar
            if (hit(layout.next, lx, ly)) {
                InvokePrimaryAction();
                return 0;
            }
            if (g_state.currentStep != InstallStep::Install && hit(layout.cancel, lx, ly)) {
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
        auto fn = reinterpret_cast<PFN_SetProcessDpiAwarenessContext>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
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

    // DPI: el diseño se hace en unidades 96 DPI y se escala por g_scale
    EnableDpiAwareness();
    g_scale = GetSystemScale();

    // Limitar la escala inicial para que la ventana quepa en pantallas pequeñas
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
