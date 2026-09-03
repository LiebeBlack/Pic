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
#include <comdef.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
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

using namespace Gdiplus;

const wchar_t APP_NAME[] = L"ARTPICST";
const wchar_t APP_VERSION[] = L"1.2.0";
const wchar_t CLASS_NAME[] = L"ARTPICSTInstallerWindow";

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
    bool createDesktopShortcut = true;
    bool createStartMenuShortcut = true;
    bool registerFileAssociations = true;
    bool isInstalling = false;
    int installProgress = 0;
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken = 0;
};

InstallerState g_state;

std::wstring GetDefaultInstallPath() {
    wchar_t programFiles[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_PROGRAM_FILES, NULL, 0, programFiles) == S_OK) {
        return std::wstring(programFiles) + L"\\" + APP_NAME;
    }
    return L"C:\\Program Files\\" + std::wstring(APP_NAME);
}

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
    wchar_t statusText[256];
    swprintf_s(statusText, L"Progreso: %d%%", g_state.installProgress);
    
    RectF statusRect(0.0f, centerY, static_cast<REAL>(client.right), 30.0f);
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
    
    RectF titleRect(0.0f, centerY - 100.0f, static_cast<REAL>(client.right), 50.0f);
    g.DrawString(L"¡Instalación Completada!", -1, &titleFont, titleRect, &format, &textBrush);
    
    const wchar_t* completeText = L"ARTPICST se ha instalado correctamente en tu sistema.\n\n"
        L"Puedes iniciar la aplicación desde el menú de inicio\n"
        L"o haciendo clic en el botón de abajo.";
    
    RectF textRect(0.0f, centerY - 30.0f, static_cast<REAL>(client.right), 60.0f);
    g.DrawString(completeText, -1, &textFont, textRect, &format, &textBrush);
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
                      L"Iniciar App", false, true);
            break;
    }
    
    // Cancel button (top right)
    RectF cancelRect(static_cast<REAL>(client.right) - 80.0f, 10.0f, 60.0f, 24.0f);
    DrawButton(g, cancelRect, L"Cancelar", false, g_state.currentStep != InstallStep::Install);
}

void PerformInstallation() {
    g_state.isInstalling = true;
    g_state.currentStep = InstallStep::Install;
    InvalidateRect(g_state.hwnd, NULL, FALSE);
    
    // Simulate installation steps
    for (int i = 0; i <= 100; i += 5) {
        g_state.installProgress = i;
        InvalidateRect(g_state.hwnd, NULL, FALSE);
        UpdateWindow(g_state.hwnd);
        Sleep(100);
    }
    
    g_state.isInstalling = false;
    g_state.currentStep = InstallStep::Complete;
    InvalidateRect(g_state.hwnd, NULL, FALSE);
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
                        // Launch application
                        ShellExecuteW(NULL, L"open", L"artpicst.exe", NULL, g_state.installPath.c_str(), SW_SHOW);
                        PostMessage(hwnd, WM_CLOSE, 0, 0);
                        break;
                    default:
                        break;
                }
                InvalidateRect(hwnd, NULL, FALSE);
            }
            
            // Back button
            RectF backButton(20.0f, buttonY, buttonWidth, buttonHeight);
            if (x >= backButton.X && x <= backButton.X + backButton.Width &&
                y >= backButton.Y && y <= backButton.Y + backButton.Height) {
                
                if (g_state.currentStep == InstallStep::License) {
                    g_state.currentStep = InstallStep::Welcome;
                    InvalidateRect(hwnd, NULL, FALSE);
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
    (void)pCmdLine;
    g_state.hInstance = hInstance;
    
    // Initialize GDI+
    GdiplusStartup(&g_state.gdiplusToken, &g_state.gdiplusStartupInput, NULL);
    
    // Register window class
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = CLASS_NAME;
    
    RegisterClassExW(&wc);
    
    // Create window
    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        CLASS_NAME,
        L"Instalador de ARTPICST",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        600, 500,
        NULL, NULL, hInstance, NULL
    );
    
    if (!hwnd) {
        GdiplusShutdown(g_state.gdiplusToken);
        return 1;
    }
    
    // Center window
    RECT rect;
    GetWindowRect(hwnd, &rect);
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(hwnd, NULL, (screenWidth - rect.right) / 2, (screenHeight - rect.bottom) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    GdiplusShutdown(g_state.gdiplusToken);
    return (int)msg.wParam;
}