// Configuración de compilación para Windows
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
#define _WIN32_WINNT 0x0601  // Windows 7+
#endif
#ifndef STBI_WINDOWS_UTF8
#define STBI_WINDOWS_UTF8
#endif

// Implementación de stb_image para carga de imágenes
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Headers de Windows API
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <gdiplus.h>
#include "../resource.h"

// Bibliotecas estándar de C++
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <iterator>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "comdlg32.lib")

using namespace Gdiplus;

// Configuración de aplicación y constantes visuales
const wchar_t CLASS_NAME[] = L"ARTPICSTWindow";
const wchar_t APP_NAME_TEXT[] = L"ARTPICST";
const wchar_t APP_VERSION_TEXT[] = L"1.1.0 Premium";

// Colores del sistema (tema oscuro)
const COLORREF BG_COLOR = RGB(10, 12, 18);
const COLORREF CHECKER_A = RGB(15, 17, 25);
const COLORREF CHECKER_B = RGB(22, 25, 35);

// Colores de interfaz Glassmorphism/Acrílica
const Color GLASS_DOCK_BG(240, 12, 14, 20);
const Color GLASS_DOCK_BORDER(130, 200, 210, 230);
const Color GLASS_DOCK_SHADOW(180, 0, 0, 0);
const Color GLASS_BTN_NORMAL(80, 200, 210, 230);
const Color GLASS_BTN_BORDER_NORMAL(65, 180, 190, 210);
const Color GLASS_BTN_HOT(250, 70, 140, 220);
const Color GLASS_BTN_BORDER_HOT(255, 130, 180, 255);
const Color GLASS_BTN_ACTIVE(250, 50, 160, 180);

// Configuración de zoom y renderizado
const float MIN_ZOOM = 0.01f;
const float MAX_ZOOM = 200.0f;
const float ZOOM_STEP = 1.25f;
const size_t CACHE_SIZE = 4;
const int MAX_DIMENSION = 30000;
const LONGLONG MAX_FILE_BYTES = 1000LL * 1024LL * 1024LL;

// Configuración de calidad máxima de renderizado
const bool ENABLE_ULTRA_QUALITY_RENDERING = true;
const bool ENABLE_ADAPTIVE_SHARPNESS = true;
const bool ENABLE_AUTO_CONTRAST = true;
const bool ENABLE_GAMMA_CORRECTION = true;

// Configuración de temporizadores
const UINT OSD_MS = 3500;
const UINT_PTR TIMER_OSD = 1;
const UINT_PTR TIMER_SLIDESHOW = 2;
const UINT SLIDESHOW_INTERVAL_MS = 3500;
const UINT_PTR TIMER_DOCK_HIDE = 3;
const UINT DOCK_HIDE_MS = 2000;
const int DOCK_PROXIMITY_THRESHOLD = 80;
const UINT_PTR TIMER_GIF = 4;

template <typename T>
struct ComPtr {
    T* p = nullptr;
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    void reset() {
        if (p) {
            p->Release();
            p = nullptr;
        }
    }
    T** operator&() {
        reset();
        return &p;
    }
    T* operator->() const { return p; }
    T* get() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

// Estructura de imagen cacheada con soporte GIF
struct CachedImage {
    unsigned char* data = nullptr;
    int width = 0, height = 0, channels = 0, rotation = 0;
    bool flipH = false, flipV = false, hasAlpha = false;
    std::wstring filepath, decoder;
    
    // Animación GIF
    bool isAnimated = false;
    int currentFrame = 0, totalFrames = 0;
    std::vector<unsigned char*> frameData;
    std::vector<int> frameDelays;

    CachedImage() = default;
    ~CachedImage() {
        if (data) { stbi_image_free(data); data = nullptr; }
        for (auto* frame : frameData) if (frame) stbi_image_free(frame);
        frameData.clear();
    }
    CachedImage(const CachedImage&) = delete;
    CachedImage& operator=(const CachedImage&) = delete;
    CachedImage(CachedImage&& other) noexcept
        : data(other.data), width(other.width), height(other.height),
          channels(other.channels), rotation(other.rotation),
          flipH(other.flipH), flipV(other.flipV), hasAlpha(other.hasAlpha),
          filepath(std::move(other.filepath)), decoder(std::move(other.decoder)),
          isAnimated(other.isAnimated), currentFrame(other.currentFrame),
          totalFrames(other.totalFrames), frameData(std::move(other.frameData)),
          frameDelays(std::move(other.frameDelays)) {
        other.data = nullptr;
        other.width = other.height = other.channels = other.rotation = 0;
        other.flipH = other.flipV = other.hasAlpha = false;
        other.isAnimated = false;
        other.currentFrame = 0;
        other.totalFrames = 0;
    }
    CachedImage& operator=(CachedImage&& other) noexcept {
        if (this != &other) {
            if (data) stbi_image_free(data);
            data = other.data;
            width = other.width; height = other.height; channels = other.channels;
            rotation = other.rotation; flipH = other.flipH; flipV = other.flipV;
            hasAlpha = other.hasAlpha;
            filepath = std::move(other.filepath); decoder = std::move(other.decoder);
            isAnimated = other.isAnimated; currentFrame = other.currentFrame;
            totalFrames = other.totalFrames;
            frameData = std::move(other.frameData); frameDelays = std::move(other.frameDelays);
            other.data = nullptr;
            other.width = other.height = other.channels = other.rotation = 0;
            other.flipH = other.flipV = other.hasAlpha = false;
            other.isAnimated = false; other.currentFrame = 0; other.totalFrames = 0;
        }
        return *this;
    }
};

enum class WindowMode { Normal, Maximized, Fullscreen };

enum HudId {
    HUD_NONE = 0,
    HUD_PREV,
    HUD_NEXT,
    HUD_FIT,
    HUD_ONE,
    HUD_ROT,
    HUD_FLIP,
    HUD_CLARITY,
    HUD_WALLPAPER,
    HUD_SAVE,
    HUD_FULL,
    HUD_OPEN
};

struct HudItem {
    HudId id = HUD_NONE;
    RECT rc{};
    const wchar_t* label = L"";
};

struct AppState {
    HWND hwnd = nullptr;
    HINSTANCE hInstance = nullptr;
    HDC hdcMem = nullptr;
    HBITMAP hbmMem = nullptr;
    HBITMAP hbmOld = nullptr;

    unsigned char* imageData = nullptr;
    int imageWidth = 0;
    int imageHeight = 0;
    int imageChannels = 0;
    int currentRotation = 0;
    bool currentFlipH = false, currentFlipV = false, hasAlpha = false;

    // Efectos de imagen
    bool effectUltraClarity = false, effectGrayscale = false, effectInvert = false;

    std::wstring currentFilePath, decoderName;
    std::vector<std::wstring> imageFiles;
    size_t currentImageIndex = 0;
    std::wstring currentFolder, startupFilePath;
    std::mutex filesMutex;

    // Vista y navegación
    float zoom = 1.0f, offsetX = 0.0f, offsetY = 0.0f;
    bool fitMode = true;

    // Arrastre de imagen
    bool isDragging = false;
    int dragStartX = 0, dragStartY = 0;
    float dragStartOffsetX = 0.0f, dragStartOffsetY = 0.0f;

    // Sistema de caché
    std::unordered_map<std::wstring, std::list<CachedImage>::iterator> cacheIndex;
    std::list<CachedImage> imageCache;
    std::mutex cacheMutex;

    // Prefetch en segundo plano
    std::thread prefetchThread;
    std::atomic<bool> prefetchRunning{false}, prefetchRequested{false};
    std::condition_variable prefetchCV;
    std::mutex prefetchMutex;
    size_t prefetchTargetIndex = 0;
    std::atomic<uint64_t> folderGeneration{0};

    bool showOSD = true;
    bool osdPinned = true;
    DWORD osdDisplayTime = 0;
    std::wstring statusMessage;
    bool isSlideshowActive = false;

    bool isFullscreen = false;
    WindowMode windowMode = WindowMode::Normal;
    WINDOWPLACEMENT windowedPlacement{};
    LONG windowedStyle = 0;
    LONG windowedExStyle = 0;

    RECT dockRect{};
    HudItem hud[12]{};
    int hudCount = 0;
    HudId hudHot = HUD_NONE;
    bool hudVisible = true;
    bool dockAutoHide = true;
    DWORD dockLastActivity = 0;

    // GIF animation support
    bool isGifAnimated = false;
    // Animación GIF
    int gifCurrentFrame = 0, gifTotalFrames = 0;
    std::vector<unsigned char*> gifFrameData;
    std::vector<int> gifFrameDelays;
    UINT_PTR gifTimer = 0;
    DWORD gifLastFrameTime = 0;

    // Sistema y recursos
    std::wstring lastError;
    ULONG_PTR gdiplusToken = 0;
    GdiplusStartupInput gdiplusStartupInput;
    bool comInitialized = false;
    HRESULT comHr = E_FAIL;
    HBRUSH classBrush = nullptr;

    AppState() {
        gdiplusStartupInput.GdiplusVersion = 1;
        windowedPlacement.length = sizeof(WINDOWPLACEMENT);
    }

    ~AppState() {
        if (imageData) { stbi_image_free(imageData); imageData = nullptr; }
    }
};

AppState g_state;

std::wstring GetFileName(const std::wstring& filepath);
std::wstring GetFileSizeString(const std::wstring& filepath);
bool PathsEqualCaseInsensitive(const std::wstring& a, const std::wstring& b);
void FreeCurrentImage();
void ScanFolderForImages(const std::wstring& folderPath);
bool LoadImageFromPath(const std::wstring& filepath);
bool LoadImageByIndex(size_t index);
void RequestPrefetch(size_t targetIndex);
void StartPrefetchThread();
void StopPrefetchThread();
void ConvertRGBAtoBGRA(unsigned char* pixels, int width, int height, bool& outHasAlpha);
void EnsureImageVisible();
void FitImageToWindow(int windowWidth, int windowHeight);
void RotateImage(int degrees = 90);
void FlipHorizontal();
void FlipVertical();
void ToggleUltraClarity();
void ToggleGrayscale();
void ToggleInvert();
void ToggleFullscreen();
void ToggleSlideshow();
void ActualSize();
void CreateDoubleBuffer(int width, int height);
void RenderImage();
void LoadInitialImageSafely();
bool CopyPathToClipboard();
bool CopyImageToClipboard();
void ShowOSD(const std::wstring& status = L"");
void NextImage();
void PreviousImage();
void OpenPath(const std::wstring& path);
void UpdateWindowTitle();
void EnableDarkTitleBar(HWND hwnd);
int ShowThemedMessageBox(HWND parent, const wchar_t* title, const wchar_t* message, UINT buttons, UINT icon);
bool OpenImageFileDialog(HWND hwnd);
bool OpenFolderDialog(HWND hwnd);
bool SaveImageDialog(HWND hwnd);
void SetAsWallpaper();
void ShowExifDialog(HWND hwnd);
void ShowProgramInfoDialog(HWND hwnd);
void LayoutHud(const RECT& client);
HudId HitTestHud(int x, int y);
void InvokeHud(HudId id);
std::wstring WideToLower(std::wstring value);
std::string WideToUtf8(const std::wstring& value);
void ApplyWindowMode(HWND hwnd, WindowMode mode);
std::wstring NormalizePath(const std::wstring& path);
void ZoomAt(float factor, int pivotX, int pivotY);
void DeleteCurrentImage();
void OpenInExplorer();

static bool SafePixelBytes(int width, int height, size_t& outBytes) {
    if (width <= 0 || height <= 0) return false;
    if (width > MAX_DIMENSION || height > MAX_DIMENSION) return false;
    const uint64_t bytes = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4ull;
    if (bytes > static_cast<uint64_t>(SIZE_MAX)) return false;
    outBytes = static_cast<size_t>(bytes);
    return true;
}

static std::wstring LogPath() {
    wchar_t temp[MAX_PATH] = {};
    DWORD n = GetTempPathW(MAX_PATH, temp);
    if (n == 0 || n >= MAX_PATH) return L"artpicst.log";
    return std::wstring(temp) + L"artpicst.log";
}

void LogMessage(const std::wstring& message) {
    try {
        std::wofstream logFile(LogPath(), std::ios::app);
        if (!logFile.is_open()) return;
        SYSTEMTIME st{};
        GetLocalTime(&st);
        wchar_t stamp[64];
        swprintf_s(stamp, L"%04u-%02u-%02u %02u:%02u:%02u",
                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        logFile << L"[" << stamp << L"] " << message << L"\n";
    } catch (...) {
    }
}

// Utilidades de conversión de texto
std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, out.data(), needed, nullptr, nullptr);
    if (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

std::wstring Utf8ToWide(const char* value) {
    if (!value || !*value) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, value, -1, nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value, -1, out.data(), needed);
    if (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

std::wstring WideToLower(std::wstring value) {
    if (!value.empty()) CharLowerBuffW(value.data(), static_cast<DWORD>(value.size()));
    return value;
}

// Utilidades de rutas
bool PathsEqualCaseInsensitive(const std::wstring& a, const std::wstring& b) {
    return WideToLower(a) == WideToLower(b);
}

std::wstring CacheKey(const std::wstring& path) { return WideToLower(path); }

std::wstring NormalizePath(const std::wstring& path) {
    if (path.empty()) return path;
    DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (needed == 0) return path;
    std::wstring full(needed, L'\0');
    DWORD written = GetFullPathNameW(path.c_str(), needed, full.data(), nullptr);
    if (written == 0 || written >= needed) return path;
    full.resize(written);
    return full;
}

// Sistema de diálogos personalizados con tema
struct ThemedDialogState {
    HWND hwnd = nullptr;
    std::wstring title;
    std::wstring message;
    UINT buttons = MB_OK;
    UINT icon = MB_ICONINFORMATION;
    int result = IDOK;
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken = 0;
};

ThemedDialogState g_dialogState;

void EnableDarkTitleBar(HWND hwnd) {
    if (!hwnd) return;
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark)); // DWMWA_USE_IMMERSIVE_DARK_MODE (Win11 / Win10 20H1+)
    DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark)); // DWMWA_USE_IMMERSIVE_DARK_MODE (Win10 1809)

    // Color de barra de título y texto premium moderno (Win 11)
    COLORREF captionColor = RGB(14, 16, 22);
    COLORREF textColor = RGB(242, 245, 250);
    DwmSetWindowAttribute(hwnd, 35, &captionColor, sizeof(captionColor)); // DWMWA_CAPTION_COLOR
    DwmSetWindowAttribute(hwnd, 36, &textColor, sizeof(textColor));       // DWMWA_TEXT_COLOR

    // Efecto de fondo Mica/Acrílico (Win 11)
    DWORD backdropType = 2; // 2 = Mica
    DwmSetWindowAttribute(hwnd, 38, &backdropType, sizeof(backdropType)); // DWMWA_SYSTEMBACKDROP_TYPE
}

LRESULT CALLBACK ThemedDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_dialogState.hwnd = hwnd;
            EnableDarkTitleBar(hwnd);
            
            // Initialize GDI+ for dialog
            GdiplusStartup(&g_dialogState.gdiplusToken, &g_dialogState.gdiplusStartupInput, nullptr);
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
            
            // Background with glass effect
            SolidBrush bgBrush(Color(240, 12, 14, 20));
            graphics.FillRectangle(&bgBrush, 0, 0, client.right, client.bottom);
            
            // Border
            Pen borderPen(Color(255, 80, 200, 210), 1.0f);
            graphics.DrawRectangle(&borderPen, 0, 0, client.right - 1, client.bottom - 1);
            
            // Title
            Gdiplus::FontFamily titleFamily(L"Segoe UI");
            Gdiplus::Font titleFont(&titleFamily, 16.0f, FontStyleBold, UnitPoint);
            StringFormat titleFormat;
            titleFormat.SetAlignment(StringAlignmentCenter);
            titleFormat.SetLineAlignment(StringAlignmentNear);
            
            SolidBrush titleBrush(Color(255, 242, 245, 250));
            RectF titleRect(0.0f, 20.0f, static_cast<float>(client.right), 40.0f);
            graphics.DrawString(g_dialogState.title.c_str(), -1, &titleFont, titleRect, &titleFormat, &titleBrush);
            
            // Message
            Gdiplus::Font messageFont(&titleFamily, 11.0f, FontStyleRegular, UnitPoint);
            StringFormat messageFormat;
            messageFormat.SetAlignment(StringAlignmentCenter);
            messageFormat.SetLineAlignment(StringAlignmentCenter);
            
            SolidBrush messageBrush(Color(255, 200, 210, 220));
            RectF messageRect(40.0f, 80.0f, static_cast<float>(client.right - 80), static_cast<float>(client.bottom - 160));
            graphics.DrawString(g_dialogState.message.c_str(), -1, &messageFont, messageRect, &messageFormat, &messageBrush);
            
            // Icon
            if (g_dialogState.icon == MB_ICONERROR) {
                SolidBrush iconBrush(Color(255, 255, 100, 100));
                graphics.FillEllipse(&iconBrush, 30, 30, 40, 40);
            } else if (g_dialogState.icon == MB_ICONWARNING) {
                SolidBrush iconBrush(Color(255, 255, 200, 100));
                graphics.FillEllipse(&iconBrush, 30, 30, 40, 40);
            } else if (g_dialogState.icon == MB_ICONQUESTION) {
                SolidBrush iconBrush(Color(255, 100, 200, 255));
                graphics.FillEllipse(&iconBrush, 30, 30, 40, 40);
            } else {
                SolidBrush iconBrush(Color(255, 100, 255, 150));
                graphics.FillEllipse(&iconBrush, 30, 30, 40, 40);
            }
            
            // Draw buttons
            int buttonWidth = 100;
            int buttonHeight = 32;
            int buttonY = client.bottom - 50;
            
            if (g_dialogState.buttons == MB_YESNO) {
                int yesX = client.right / 2 - buttonWidth - 10;
                int noX = client.right / 2 + 10;
                
                // Yes button
                SolidBrush yesBrush(Color(255, 48, 120, 235));
                graphics.FillRectangle(&yesBrush, static_cast<float>(yesX), static_cast<float>(buttonY), static_cast<float>(buttonWidth), static_cast<float>(buttonHeight));
                Pen yesBorder(Color(255, 80, 200, 210), 1.0f);
                graphics.DrawRectangle(&yesBorder, static_cast<float>(yesX), static_cast<float>(buttonY), static_cast<float>(buttonWidth), static_cast<float>(buttonHeight));
                
                // No button
                SolidBrush noBrush(Color(255, 55, 55, 55));
                graphics.FillRectangle(&noBrush, static_cast<float>(noX), static_cast<float>(buttonY), static_cast<float>(buttonWidth), static_cast<float>(buttonHeight));
                Pen noBorder(Color(255, 80, 80, 80), 1.0f);
                graphics.DrawRectangle(&noBorder, static_cast<float>(noX), static_cast<float>(buttonY), static_cast<float>(buttonWidth), static_cast<float>(buttonHeight));
                
                // Button text
                Gdiplus::Font buttonFont(&titleFamily, 11.0f, FontStyleRegular, UnitPoint);
                StringFormat buttonFormat;
                buttonFormat.SetAlignment(StringAlignmentCenter);
                buttonFormat.SetLineAlignment(StringAlignmentCenter);
                
                SolidBrush buttonTextBrush(Color(255, 242, 245, 250));
                RectF yesTextRect(static_cast<float>(yesX), static_cast<float>(buttonY), static_cast<float>(buttonWidth), static_cast<float>(buttonHeight));
                graphics.DrawString(L"Sí", -1, &buttonFont, yesTextRect, &buttonFormat, &buttonTextBrush);
                
                RectF noTextRect(static_cast<float>(noX), static_cast<float>(buttonY), static_cast<float>(buttonWidth), static_cast<float>(buttonHeight));
                graphics.DrawString(L"No", -1, &buttonFont, noTextRect, &buttonFormat, &buttonTextBrush);
            } else {
                int okX = client.right / 2 - buttonWidth / 2;
                
                // OK button
                SolidBrush okBrush(Color(255, 48, 120, 235));
                graphics.FillRectangle(&okBrush, static_cast<float>(okX), static_cast<float>(buttonY), static_cast<float>(buttonWidth), static_cast<float>(buttonHeight));
                Pen okBorder(Color(255, 80, 200, 210), 1.0f);
                graphics.DrawRectangle(&okBorder, static_cast<float>(okX), static_cast<float>(buttonY), static_cast<float>(buttonWidth), static_cast<float>(buttonHeight));
                
                // Button text
                Gdiplus::Font buttonFont(&titleFamily, 11.0f, FontStyleRegular, UnitPoint);
                StringFormat buttonFormat;
                buttonFormat.SetAlignment(StringAlignmentCenter);
                buttonFormat.SetLineAlignment(StringAlignmentCenter);
                
                SolidBrush buttonTextBrush(Color(255, 242, 245, 250));
                RectF okTextRect(static_cast<float>(okX), static_cast<float>(buttonY), static_cast<float>(buttonWidth), static_cast<float>(buttonHeight));
                graphics.DrawString(L"Aceptar", -1, &buttonFont, okTextRect, &buttonFormat, &buttonTextBrush);
            }
            
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return TRUE;
        case WM_DESTROY:
            if (g_dialogState.gdiplusToken) {
                GdiplusShutdown(g_dialogState.gdiplusToken);
                g_dialogState.gdiplusToken = 0;
            }
            PostQuitMessage(0);
            return 0;
        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            
            RECT client;
            GetClientRect(hwnd, &client);
            
            int buttonWidth = 100;
            int buttonHeight = 32;
            int buttonY = client.bottom - 50;
            
            // Check button clicks
            if (g_dialogState.buttons == MB_YESNO) {
                int yesX = client.right / 2 - buttonWidth - 10;
                int noX = client.right / 2 + 10;
                
                if (x >= yesX && x <= yesX + buttonWidth && y >= buttonY && y <= buttonY + buttonHeight) {
                    g_dialogState.result = IDYES;
                    PostMessage(hwnd, WM_CLOSE, 0, 0);
                } else if (x >= noX && x <= noX + buttonWidth && y >= buttonY && y <= buttonY + buttonHeight) {
                    g_dialogState.result = IDNO;
                    PostMessage(hwnd, WM_CLOSE, 0, 0);
                }
            } else {
                int okX = client.right / 2 - buttonWidth / 2;
                if (x >= okX && x <= okX + buttonWidth && y >= buttonY && y <= buttonY + buttonHeight) {
                    g_dialogState.result = IDOK;
                    PostMessage(hwnd, WM_CLOSE, 0, 0);
                }
            }
            return 0;
        }
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

int ShowThemedMessageBox(HWND parent, const wchar_t* title, const wchar_t* message, UINT buttons, UINT icon) {
    static bool classRegistered = false;
    static const wchar_t* DIALOG_CLASS = L"ARTPICSTThemedDialog";
    
    if (!classRegistered) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = ThemedDialogProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = DIALOG_CLASS;
        RegisterClassExW(&wc);
        classRegistered = true;
    }
    
    g_dialogState.title = title;
    g_dialogState.message = message;
    g_dialogState.buttons = buttons;
    g_dialogState.icon = icon;
    g_dialogState.result = IDOK;
    
    // Calculate dialog size
    int width = 400;
    int height = 250;
    
    // Center dialog on parent or screen
    RECT parentRect;
    if (parent) {
        GetWindowRect(parent, &parentRect);
    } else {
        parentRect.left = 0;
        parentRect.top = 0;
        parentRect.right = GetSystemMetrics(SM_CXSCREEN);
        parentRect.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    
    int x = parentRect.left + (parentRect.right - parentRect.left - width) / 2;
    int y = parentRect.top + (parentRect.bottom - parentRect.top - height) / 2;
    
    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW | WS_EX_DLGMODALFRAME,
        DIALOG_CLASS,
        title,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, width, height,
        parent, nullptr, GetModuleHandle(nullptr), nullptr
    );
    
    if (!hwnd) return IDOK;
    
    EnableWindow(parent ? parent : GetActiveWindow(), FALSE);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    EnableWindow(parent ? parent : GetActiveWindow(), TRUE);
    if (parent) SetForegroundWindow(parent);
    
    return g_dialogState.result;
}

// Inicialización de GDI+
bool InitGDIPlus() {
    return GdiplusStartup(&g_state.gdiplusToken, &g_state.gdiplusStartupInput, nullptr) == Ok;
}

// Gestión de buffers y recursos
void FreeDoubleBuffer() {
    if (!g_state.hdcMem) return;
    if (g_state.hbmOld) { SelectObject(g_state.hdcMem, g_state.hbmOld); g_state.hbmOld = nullptr; }
    if (g_state.hbmMem) { DeleteObject(g_state.hbmMem); g_state.hbmMem = nullptr; }
    DeleteDC(g_state.hdcMem);
    g_state.hdcMem = nullptr;
}

void StopPrefetchThread() {
    g_state.prefetchRunning = false;
    g_state.prefetchRequested = true;
    g_state.prefetchCV.notify_all();
    if (g_state.prefetchThread.joinable()) g_state.prefetchThread.join();
}

void CleanupGDIPlus() {
    StopPrefetchThread();
    {
        std::lock_guard<std::mutex> lock(g_state.cacheMutex);
        g_state.imageCache.clear();
        g_state.cacheIndex.clear();
    }
    FreeCurrentImage();
    FreeDoubleBuffer();
    if (g_state.gdiplusToken) {
        GdiplusShutdown(g_state.gdiplusToken);
        g_state.gdiplusToken = 0;
    }
}

bool CopyCachedPixels(const CachedImage& src, unsigned char*& dest) {
    size_t bytes = 0;
    if (!src.data || !SafePixelBytes(src.width, src.height, bytes)) return false;
    dest = static_cast<unsigned char*>(malloc(bytes));
    if (!dest) return false;
    memcpy(dest, src.data, bytes);
    return true;
}

bool TryCopyFromCache(const std::wstring& filepath, CachedImage& outCopy) {
    const std::wstring key = CacheKey(filepath);
    std::lock_guard<std::mutex> lock(g_state.cacheMutex);
    auto it = g_state.cacheIndex.find(key);
    if (it == g_state.cacheIndex.end() || it->second == g_state.imageCache.end()) {
        return false;
    }
    g_state.imageCache.splice(g_state.imageCache.end(), g_state.imageCache, it->second);
    it->second = std::prev(g_state.imageCache.end());
    const CachedImage& src = *it->second;
    unsigned char* pixels = nullptr;
    if (!CopyCachedPixels(src, pixels)) return false;
    outCopy.data = pixels;
    outCopy.width = src.width;
    outCopy.height = src.height;
    outCopy.channels = src.channels;
    outCopy.rotation = src.rotation;
    outCopy.flipH = src.flipH;
    outCopy.flipV = src.flipV;
    outCopy.hasAlpha = src.hasAlpha;
    outCopy.filepath = src.filepath;
    outCopy.decoder = src.decoder;
    return true;
}

void AddToCache(const std::wstring& filepath, unsigned char* data, int width, int height,
                int channels, int rotation, bool flipH, bool flipV, bool hasAlpha, const std::wstring& decoder) {
    if (!data) return;
    size_t bytes = 0;
    if (!SafePixelBytes(width, height, bytes)) {
        stbi_image_free(data);
        return;
    }
    std::lock_guard<std::mutex> lock(g_state.cacheMutex);
    const std::wstring key = CacheKey(filepath);
    auto it = g_state.cacheIndex.find(key);
    if (it != g_state.cacheIndex.end()) {
        if (it->second != g_state.imageCache.end()) g_state.imageCache.erase(it->second);
        g_state.cacheIndex.erase(it);
    }
    CachedImage cached;
    cached.data = data;
    cached.width = width;
    cached.height = height;
    cached.channels = channels;
    cached.rotation = rotation;
    cached.flipH = flipH;
    cached.flipV = flipV;
    cached.hasAlpha = hasAlpha;
    cached.filepath = filepath;
    cached.decoder = decoder;
    g_state.imageCache.push_back(std::move(cached));
    g_state.cacheIndex[key] = std::prev(g_state.imageCache.end());
    while (g_state.imageCache.size() > CACHE_SIZE) {
        auto oldest = g_state.imageCache.begin();
        g_state.cacheIndex.erase(CacheKey(oldest->filepath));
        g_state.imageCache.pop_front();
    }
}

bool IsInCache(const std::wstring& filepath) {
    std::lock_guard<std::mutex> lock(g_state.cacheMutex);
    return g_state.cacheIndex.find(CacheKey(filepath)) != g_state.cacheIndex.end();
}

void ClearCache() {
    std::lock_guard<std::mutex> lock(g_state.cacheMutex);
    g_state.imageCache.clear();
    g_state.cacheIndex.clear();
}

void FreeCurrentImage() {
    if (g_state.imageData) {
        stbi_image_free(g_state.imageData);
        g_state.imageData = nullptr;
    }
    g_state.imageWidth = 0;
    g_state.imageHeight = 0;
    g_state.imageChannels = 0;
    g_state.currentRotation = 0;
    g_state.currentFlipH = false;
    g_state.currentFlipV = false;
    g_state.hasAlpha = false;
    g_state.effectUltraClarity = false;
    g_state.effectGrayscale = false;
    g_state.effectInvert = false;
    g_state.currentFilePath.clear();
    g_state.decoderName.clear();
}

bool ValidateFileIntegrity(const std::wstring& filepath) {
    WIN32_FILE_ATTRIBUTE_DATA fileData{};
    if (!GetFileAttributesExW(filepath.c_str(), GetFileExInfoStandard, &fileData)) return false;
    if (fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return false;
    LARGE_INTEGER size{};
    size.HighPart = fileData.nFileSizeHigh;
    size.LowPart = fileData.nFileSizeLow;
    if (size.QuadPart <= 0 || size.QuadPart > MAX_FILE_BYTES) return false;
    HANDLE hFile = CreateFileW(filepath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    CloseHandle(hFile);
    return true;
}

void ShowOSD(const std::wstring& status) {
    g_state.showOSD = true;
    g_state.osdDisplayTime = GetTickCount();
    if (status.empty()) {
        g_state.statusMessage.clear();
    } else {
        g_state.statusMessage = status;
    }
    if (g_state.hwnd) {
        if (!g_state.osdPinned) {
            SetTimer(g_state.hwnd, TIMER_OSD, OSD_MS + 80, nullptr);
        }
        InvalidateRect(g_state.hwnd, nullptr, FALSE);
    }
}

void HandleError(const std::wstring& errorMsg, bool showUser) {
    g_state.lastError = errorMsg;
    LogMessage(errorMsg);
    ShowOSD(errorMsg);
    if (showUser && g_state.hwnd) {
        static DWORD lastErrorTime = 0;
        DWORD now = GetTickCount();
        if (now - lastErrorTime > 2500) {
            lastErrorTime = now;
            ShowThemedMessageBox(g_state.hwnd, APP_NAME_TEXT, errorMsg.c_str(), MB_OK, MB_ICONWARNING);
        }
    }
}

void ShowAboutDialog(HWND hwnd) {
    std::wstring info =
        std::wstring(APP_NAME_TEXT) + L" v" + APP_VERSION_TEXT + L" (Edición Premium)\n\n" +
        L"Visor de imágenes nativo con aceleración por hardware y diseño Acrílico/Glassmorphism.\n\n" +
        L"• Motores: stb_image, WIC y GDI+ con interpolación bicúbica de precisión y Ultra-Claridad HDR.\n" +
        L"• Auto-orientación EXIF, visor de máxima área, fondo de pantalla directo y exportador PNG/JPG.\n" +
        L"• Actualizador integrado sin bucles y soporte para más de 30 formatos gráficos.\n\n" +
        L"© 2026 ARTPICST";
    ShowThemedMessageBox(hwnd ? hwnd : nullptr, APP_NAME_TEXT, info.c_str(), MB_OK, MB_ICONINFORMATION);
}

void ShowProgramInfoDialog(HWND hwnd) {
    std::wstring info =
        std::wstring(APP_NAME_TEXT) + L" v" + APP_VERSION_TEXT + L" — Guía Completa de Uso y Controles\n\n"
        L"⌨️ ATAJOS DE TECLADO:\n"
        L"• ◀ ▶ : Imagen anterior / siguiente\n"
        L"• ▲ ▼ : Acercar / alejar zoom\n"
        L"• Espacio / Retroceso : Imagen siguiente / anterior\n"
        L"• Inicio / Fin : Primera / última imagen de la carpeta\n"
        L"• Rueda / '+' / '-' : Acercar y alejar zoom fluido centrado\n"
        L"• F : Ajustar imagen a toda la pantalla/ventana\n"
        L"• 1 o 0 : Tamaño real al 100% (píxel a píxel)\n"
        L"• D : Activar / desactivar modo Ultra-Claridad (Detalles HDR)\n"
        L"• R / Shift+R : Rotar 90° (horario / antihorario)\n"
        L"• H / V : Volteo horizontal / vertical\n"
        L"• G : Alternar escala de grises (Blanco y Negro)\n"
        L"• N : Alternar colores invertidos (Negativo)\n"
        L"• F5 : Iniciar o detener presentación (pase de diapositivas)\n"
        L"• F11 o Doble clic : Pantalla completa (ESC para salir)\n"
        L"• Supr (Delete) : Enviar imagen a la Papelera de reciclaje\n"
        L"• E o Ctrl+I : Ver metadatos detallados y propiedades EXIF\n"
        L"• Ctrl+S : Guardar / Exportar imagen (PNG, JPG, BMP)\n"
        L"• Ctrl+W : Establecer como fondo de pantalla de Windows\n"
        L"• Ctrl+E : Mostrar y seleccionar en el Explorador de Windows\n"
        L"• Ctrl+C / Ctrl+Shift+C : Copiar imagen / copiar ruta completa\n"
        L"• Ctrl+O / Ctrl+Shift+O : Abrir archivo de imagen / abrir carpeta\n"
        L"• Ctrl+U : Comprobar e instalar actualizaciones\n"
        L"• I : Fijar o alternar barra de información flotante\n\n"
        L"🖱️ CONTROLES DE RATÓN:\n"
        L"• Clic izquierdo + Arrastrar : Mover / desplazar imagen (Pan) siempre disponible\n"
        L"• Rueda del ratón : Zoom inteligente centrado en el cursor\n"
        L"• Clic central (rueda) : Alternar entre 100% y ajuste\n"
        L"• Botones laterales X1 / X2 : Imagen anterior / siguiente\n"
        L"• Doble clic : Pantalla completa\n\n"
        L"🎛️ BOTONES DEL PANEL FLOTANTE (DOCK):\n"
        L"• ◀ ▶ : Navegación de imágenes en la carpeta\n"
        L"• Ajustar : Ajusta la foto al máximo tamaño de la ventana\n"
        L"• 1:1 : Visualización a resolución nativa\n"
        L"• Claridad : Modo Ultra-Claridad / realce de detalles finos\n"
        L"• Rotar / Voltear : Transformaciones geométricas\n"
        L"• Fondo : Aplica la foto como fondo de escritorio de Windows\n"
        L"• Guardar : Exporta la foto con transformaciones aplicadas\n"
        L"• Pantalla : Alterna pantalla completa / ventana\n"
        L"• Abrir : Diálogo para abrir nueva imagen o carpeta\n\n"
        L"🚀 MOTORES Y COMPATIBILIDAD:\n"
        L"• Decodificadores: stb_image, Windows Imaging Component (WIC) y GDI+\n"
        L"• Formatos: JPG, PNG, WebP, HEIC/HEIF, AVIF, BMP, GIF, TIFF, ICO, TGA, PSD, HDR, RAW y más.\n"
        L"• Calidad: Anti-aliasing bicúbico, auto-orientación EXIF y transparencia premium.";

    ShowThemedMessageBox(hwnd, L"Info — ARTPICST (Atajos, Funciones y Controles)", info.c_str(), MB_OK, MB_ICONINFORMATION);
}

bool IsSupportedImageExtension(const std::wstring& extLower) {
    static const wchar_t* supported[] = {
        L"jpg", L"jpeg", L"jpe", L"jfif", L"jif",
        L"png", L"apng",
        L"bmp", L"dib", L"rle",
        L"gif",
        L"tif", L"tiff",
        L"webp",
        L"heic", L"heif", L"hif",
        L"avif",
        L"jxl", L"jxr", L"wdp", L"hdp",
        L"ico", L"cur", L"icon",
        L"tga", L"tpic",
        L"psd",
        L"hdr", L"pic",
        L"pnm", L"ppm", L"pgm", L"pbm",
        L"jp2", L"j2k", L"jpx",
        L"emf", L"wmf", L"exif",
        L"dng", L"cr2", L"cr3", L"nef", L"arw", L"orf", L"rw2", L"raf", L"sr2", L"kdc", L"raw"
    };
    for (const wchar_t* item : supported) {
        if (extLower == item) return true;
    }
    return false;
}

std::wstring GetExtensionLower(const std::wstring& filename) {
    const size_t dot = filename.find_last_of(L'.');
    if (dot == std::wstring::npos || dot + 1 >= filename.size()) return {};
    return WideToLower(filename.substr(dot + 1));
}

void ScanFolderForImages(const std::wstring& folderPath) {
    std::vector<std::wstring> found;
    WIN32_FIND_DATAW findData{};
    const std::wstring searchPath = folderPath + L"\\*";
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            const std::wstring filename = findData.cFileName;
            const std::wstring ext = GetExtensionLower(filename);
            if (ext.empty() || !IsSupportedImageExtension(ext)) continue;
            found.push_back(folderPath + L"\\" + filename);
        } while (FindNextFileW(hFind, &findData));
        FindClose(hFind);
    }
    // Orden natural inteligente de Windows
    std::sort(found.begin(), found.end(), [](const std::wstring& a, const std::wstring& b) {
        return StrCmpLogicalW(a.c_str(), b.c_str()) < 0;
    });
    {
        std::lock_guard<std::mutex> lock(g_state.filesMutex);
        g_state.imageFiles = std::move(found);
        g_state.currentFolder = folderPath;
        g_state.currentImageIndex = 0;
    }
    g_state.folderGeneration.fetch_add(1, std::memory_order_relaxed);
    ClearCache();
}

size_t ImageCount() {
    std::lock_guard<std::mutex> lock(g_state.filesMutex);
    return g_state.imageFiles.size();
}

std::wstring ImagePathAt(size_t index) {
    std::lock_guard<std::mutex> lock(g_state.filesMutex);
    if (index >= g_state.imageFiles.size()) return {};
    return g_state.imageFiles[index];
}

bool FindImageIndex(const std::wstring& path, size_t& outIndex) {
    std::lock_guard<std::mutex> lock(g_state.filesMutex);
    for (size_t i = 0; i < g_state.imageFiles.size(); ++i) {
        if (PathsEqualCaseInsensitive(g_state.imageFiles[i], path)) {
            outIndex = i;
            return true;
        }
    }
    return false;
}

void EnsureFileInList(const std::wstring& path) {
    size_t dummy = 0;
    if (FindImageIndex(path, dummy)) return;
    std::lock_guard<std::mutex> lock(g_state.filesMutex);
    g_state.imageFiles.push_back(path);
    g_state.currentImageIndex = g_state.imageFiles.size() - 1;
}

int GetExifOrientationFromJpeg(const std::wstring& filepath) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, filepath.c_str(), L"rb") != 0 || !f) return 1;

    unsigned char header[2];
    if (fread(header, 1, 2, f) != 2 || header[0] != 0xFF || header[1] != 0xD8) {
        fclose(f);
        return 1;
    }

    int orientation = 1;
    while (true) {
        unsigned char marker[2];
        if (fread(marker, 1, 2, f) != 2 || marker[0] != 0xFF) break;
        if (marker[1] == 0xDA || marker[1] == 0xD9) break;

        unsigned char lenBytes[2];
        if (fread(lenBytes, 1, 2, f) != 2) break;
        int len = (lenBytes[0] << 8) | lenBytes[1];
        if (len < 2) break;

        if (marker[1] == 0xE1 && len >= 14) {
            std::vector<unsigned char> data(len - 2);
            if (fread(data.data(), 1, len - 2, f) == static_cast<size_t>(len - 2)) {
                if (memcmp(data.data(), "Exif\0\0", 6) == 0) {
                    const unsigned char* tiff = data.data() + 6;
                    const size_t tiffLen = data.size() - 6;
                    if (tiffLen >= 8) {
                        bool littleEndian = (tiff[0] == 'I' && tiff[1] == 'I');
                        auto read16 = [littleEndian](const unsigned char* p) -> uint16_t {
                            return littleEndian ? (p[0] | (p[1] << 8)) : ((p[0] << 8) | p[1]);
                        };
                        auto read32 = [littleEndian](const unsigned char* p) -> uint32_t {
                            return littleEndian ? (p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24))
                                                : ((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
                        };
                        uint32_t ifdOffset = read32(tiff + 4);
                        if (ifdOffset + 2 <= tiffLen) {
                            uint16_t tagCount = read16(tiff + ifdOffset);
                            const unsigned char* tagPtr = tiff + ifdOffset + 2;
                            for (uint16_t i = 0; i < tagCount && (tagPtr + 12 <= tiff + tiffLen); ++i, tagPtr += 12) {
                                uint16_t tag = read16(tagPtr);
                                if (tag == 0x0112) {
                                    orientation = read16(tagPtr + 8);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            break;
        } else {
            fseek(f, len - 2, SEEK_CUR);
        }
    }
    fclose(f);
    return orientation;
}

void ConvertRGBAtoBGRA(unsigned char* pixels, int width, int height, bool& outHasAlpha) {
    outHasAlpha = false;
    if (!pixels || width <= 0 || height <= 0) return;
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
    for (size_t i = 0; i < count; ++i) {
        unsigned char* p = pixels + (i * 4);
        std::swap(p[0], p[2]);
        if (p[3] < 255) {
            outHasAlpha = true;
        }
    }
}

unsigned char* DecodeWithStb(const std::wstring& filepath, int& width, int& height, int& channels, bool& outHasAlpha, int& autoRotateDeg) {
    width = height = channels = 0;
    outHasAlpha = false;
    autoRotateDeg = 0;
    const std::string utf8 = WideToUtf8(filepath);
    if (utf8.empty()) return nullptr;
    
    // Check if this is a GIF file
    const std::wstring ext = GetExtensionLower(filepath);
    bool isGif = (ext == L"gif");
    
    unsigned char* pixels = stbi_load(utf8.c_str(), &width, &height, &channels, 4);
    if (!pixels) return nullptr;
    size_t bytes = 0;
    if (!SafePixelBytes(width, height, bytes)) {
        stbi_image_free(pixels);
        width = height = channels = 0;
        return nullptr;
    }
    ConvertRGBAtoBGRA(pixels, width, height, outHasAlpha);

    // Handle GIF animation state
    if (isGif) {
        g_state.isGifAnimated = true;
        g_state.gifCurrentFrame = 0;
        g_state.gifTotalFrames = 1; // For now, single frame
        g_state.gifLastFrameTime = GetTickCount();
    } else {
        g_state.isGifAnimated = false;
        g_state.gifCurrentFrame = 0;
        g_state.gifTotalFrames = 0;
        
        int exif = GetExifOrientationFromJpeg(filepath);
        if (exif == 6) autoRotateDeg = 90;
        else if (exif == 3) autoRotateDeg = 180;
        else if (exif == 8) autoRotateDeg = 270;
    }

    return pixels;
}

unsigned char* DecodeWithWic(const std::wstring& filepath, int& width, int& height, int& channels, bool& outHasAlpha) {
    width = height = channels = 0;
    outHasAlpha = false;
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory.p));
    if (FAILED(hr) || !factory) return nullptr;

    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(filepath.c_str(), nullptr, GENERIC_READ,
                                            WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || !decoder) return nullptr;

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) return nullptr;

    ComPtr<IWICBitmapSource> source;
    ComPtr<IWICBitmapFlipRotator> rotator;
    if (SUCCEEDED(factory->CreateBitmapFlipRotator(&rotator)) && rotator) {
        if (SUCCEEDED(rotator->Initialize(frame.get(), WICBitmapTransformRotate0))) {
            source.p = rotator.p;
            rotator.p->AddRef();
        }
    }
    if (!source) {
        source.p = frame.get();
        frame.p->AddRef();
    }

    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr) || !converter) return nullptr;

    hr = converter->Initialize(source.get(), GUID_WICPixelFormat32bppBGRA,
                               WICBitmapDitherTypeNone, nullptr, 0.0,
                               WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return nullptr;

    UINT w = 0, h = 0;
    hr = converter->GetSize(&w, &h);
    if (FAILED(hr)) return nullptr;

    size_t bytes = 0;
    if (!SafePixelBytes(static_cast<int>(w), static_cast<int>(h), bytes)) return nullptr;

    unsigned char* pixels = static_cast<unsigned char*>(malloc(bytes));
    if (!pixels) return nullptr;
    const UINT stride = w * 4;
    hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(bytes), pixels);
    if (FAILED(hr)) {
        free(pixels);
        return nullptr;
    }
    width = static_cast<int>(w);
    height = static_cast<int>(h);
    channels = 4;

    const size_t totalPixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    for (size_t i = 0; i < totalPixels; ++i) {
        if (pixels[i * 4 + 3] < 255) {
            outHasAlpha = true;
            break;
        }
    }

    return pixels;
}

unsigned char* DecodeWithGdiplus(const std::wstring& filepath, int& width, int& height, int& channels, bool& outHasAlpha) {
    width = height = channels = 0;
    outHasAlpha = false;
    Bitmap bmp(filepath.c_str());
    if (bmp.GetLastStatus() != Ok) return nullptr;

    UINT propSize = bmp.GetPropertyItemSize(PropertyTagOrientation);
    if (propSize > 0) {
        PropertyItem* prop = static_cast<PropertyItem*>(malloc(propSize));
        if (prop && bmp.GetPropertyItem(PropertyTagOrientation, propSize, prop) == Ok) {
            short orientation = *reinterpret_cast<short*>(prop->value);
            switch (orientation) {
                case 2: bmp.RotateFlip(RotateNoneFlipX); break;
                case 3: bmp.RotateFlip(Rotate180FlipNone); break;
                case 4: bmp.RotateFlip(Rotate180FlipX); break;
                case 5: bmp.RotateFlip(Rotate90FlipX); break;
                case 6: bmp.RotateFlip(Rotate90FlipNone); break;
                case 7: bmp.RotateFlip(Rotate270FlipX); break;
                case 8: bmp.RotateFlip(Rotate270FlipNone); break;
                default: break;
            }
        }
        free(prop);
    }

    const int w = static_cast<int>(bmp.GetWidth());
    const int h = static_cast<int>(bmp.GetHeight());
    size_t bytes = 0;
    if (!SafePixelBytes(w, h, bytes)) return nullptr;

    Rect rect(0, 0, w, h);
    BitmapData data{};
    if (bmp.LockBits(&rect, ImageLockModeRead, PixelFormat32bppARGB, &data) != Ok) return nullptr;

    unsigned char* pixels = static_cast<unsigned char*>(malloc(bytes));
    if (!pixels) {
        bmp.UnlockBits(&data);
        return nullptr;
    }
    const UINT dstStride = static_cast<UINT>(w * 4);
    auto* src = static_cast<unsigned char*>(data.Scan0);
    const INT srcStride = data.Stride;
    for (int y = 0; y < h; ++y) {
        memcpy(pixels + static_cast<size_t>(y) * dstStride,
               src + static_cast<ptrdiff_t>(y) * static_cast<ptrdiff_t>(srcStride),
               dstStride);
    }
    bmp.UnlockBits(&data);
    width = w;
    height = h;
    channels = 4;

    const size_t totalPixels = static_cast<size_t>(w) * static_cast<size_t>(h);
    for (size_t i = 0; i < totalPixels; ++i) {
        if (pixels[i * 4 + 3] < 255) {
            outHasAlpha = true;
            break;
        }
    }

    return pixels;
}

unsigned char* DecodeImageFile(const std::wstring& filepath, int& width, int& height, int& channels, bool& hasAlpha, int& autoRotateDeg, std::wstring& decoder) {
    decoder.clear();
    autoRotateDeg = 0;
    unsigned char* pixels = DecodeWithStb(filepath, width, height, channels, hasAlpha, autoRotateDeg);
    if (pixels) {
        decoder = L"stb";
        return pixels;
    }
    pixels = DecodeWithWic(filepath, width, height, channels, hasAlpha);
    if (pixels) {
        decoder = L"WIC";
        return pixels;
    }
    pixels = DecodeWithGdiplus(filepath, width, height, channels, hasAlpha);
    if (pixels) {
        decoder = L"GDI+";
        return pixels;
    }
    return nullptr;
}

void StoreCurrentInCache() {
    if (g_state.currentFilePath.empty() || !g_state.imageData) return;
    size_t bytes = 0;
    if (!SafePixelBytes(g_state.imageWidth, g_state.imageHeight, bytes)) return;
    unsigned char* copy = static_cast<unsigned char*>(malloc(bytes));
    if (!copy) return;
    memcpy(copy, g_state.imageData, bytes);
    AddToCache(g_state.currentFilePath, copy, g_state.imageWidth, g_state.imageHeight,
               g_state.imageChannels, g_state.currentRotation, g_state.currentFlipH,
               g_state.currentFlipV, g_state.hasAlpha, g_state.decoderName);
}

void UpdateWindowTitle() {
    if (!g_state.hwnd) return;
    std::wstring title = L"ARTPICST";
    if (!g_state.currentFilePath.empty()) {
        title += L" — ";
        title += GetFileName(g_state.currentFilePath);
        const size_t count = ImageCount();
        if (count > 0) {
            wchar_t extra[64];
            swprintf_s(extra, L"  (%zu/%zu)", g_state.currentImageIndex + 1, count);
            title += extra;
        }
    }
    SetWindowTextW(g_state.hwnd, title.c_str());
}

bool ApplyLoadedImage(unsigned char* pixels, int width, int height, int channels, int rotation,
                      bool flipH, bool flipV, bool hasAlpha, const std::wstring& filepath, const std::wstring& decoder) {
    FreeCurrentImage();
    g_state.imageData = pixels;
    g_state.imageWidth = width;
    g_state.imageHeight = height;
    g_state.imageChannels = channels;
    g_state.currentRotation = rotation;
    g_state.currentFlipH = flipH;
    g_state.currentFlipV = flipV;
    g_state.hasAlpha = hasAlpha;
    g_state.currentFilePath = filepath;
    g_state.decoderName = decoder;
    if (g_state.hwnd) {
        RECT client{};
        GetClientRect(g_state.hwnd, &client);
        FitImageToWindow(client.right - client.left, client.bottom - client.top);
    }
    UpdateWindowTitle();
    ShowOSD();
    return true;
}

bool LoadImageFromPath(const std::wstring& filepath) {
    if (!ValidateFileIntegrity(filepath)) {
        HandleError(L"Archivo inválido o inaccesible: " + GetFileName(filepath), false);
        return false;
    }

    CachedImage cached;
    if (TryCopyFromCache(filepath, cached) && cached.data) {
        ApplyLoadedImage(cached.data, cached.width, cached.height, cached.channels, cached.rotation,
                         cached.flipH, cached.flipV, cached.hasAlpha, filepath, cached.decoder);
        cached.data = nullptr;
        return true;
    }

    int width = 0, height = 0, channels = 0, autoRotate = 0;
    bool hasAlpha = false;
    std::wstring decoder;
    unsigned char* pixels = DecodeImageFile(filepath, width, height, channels, hasAlpha, autoRotate, decoder);
    if (!pixels) {
        std::wstring msg = L"No se pudo cargar: " + GetFileName(filepath);
        HandleError(msg, false);
        return false;
    }

    ApplyLoadedImage(pixels, width, height, channels, autoRotate, false, false, hasAlpha, filepath, decoder);
    StoreCurrentInCache();
    return true;
}

bool LoadImageByIndex(size_t index) {
    const std::wstring path = ImagePathAt(index);
    if (path.empty()) return false;
    {
        std::lock_guard<std::mutex> lock(g_state.filesMutex);
        g_state.currentImageIndex = index;
    }
    return LoadImageFromPath(path);
}

void LoadInitialImageSafely() {
    if (!g_state.startupFilePath.empty()) {
        size_t idx = 0;
        if (FindImageIndex(g_state.startupFilePath, idx)) {
            LoadImageByIndex(idx);
            RequestPrefetch(idx);
            return;
        }
        if (LoadImageFromPath(g_state.startupFilePath)) {
            EnsureFileInList(g_state.startupFilePath);
            return;
        }
    }
    if (ImageCount() > 0) {
        LoadImageByIndex(0);
        RequestPrefetch(0);
    } else {
        UpdateWindowTitle();
    }
}

void RequestPrefetch(size_t targetIndex) {
    if (!g_state.prefetchRunning) return;
    std::lock_guard<std::mutex> lock(g_state.prefetchMutex);
    g_state.prefetchTargetIndex = targetIndex;
    g_state.prefetchRequested = true;
    g_state.prefetchCV.notify_one();
}

void PrefetchThreadFunc() {
    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    while (g_state.prefetchRunning) {
        std::unique_lock<std::mutex> lock(g_state.prefetchMutex);
        g_state.prefetchCV.wait_for(lock, std::chrono::milliseconds(400), [] {
            return g_state.prefetchRequested.load() || !g_state.prefetchRunning.load();
        });
        if (!g_state.prefetchRunning) break;
        if (!g_state.prefetchRequested) continue;
        g_state.prefetchRequested = false;
        const size_t targetIndex = g_state.prefetchTargetIndex;
        lock.unlock();

        const uint64_t generation = g_state.folderGeneration.load(std::memory_order_relaxed);
        const size_t count = ImageCount();
        if (count == 0) continue;
        const size_t indices[4] = {
            (targetIndex + 1) % count,
            (targetIndex == 0) ? count - 1 : targetIndex - 1,
            (targetIndex + 2) % count,
            (targetIndex < 2) ? count - (2 - targetIndex) : targetIndex - 2
        };
        for (size_t idx : indices) {
            if (!g_state.prefetchRunning) break;
            if (generation != g_state.folderGeneration.load(std::memory_order_relaxed)) break;
            const std::wstring filepath = ImagePathAt(idx);
            if (filepath.empty() || IsInCache(filepath)) continue;
            if (!ValidateFileIntegrity(filepath)) continue;
            int width = 0, height = 0, channels = 0, autoRotate = 0;
            bool hasAlpha = false;
            std::wstring decoder;
            unsigned char* pixels = DecodeImageFile(filepath, width, height, channels, hasAlpha, autoRotate, decoder);
            if (!pixels) continue;
            if (generation != g_state.folderGeneration.load(std::memory_order_relaxed)) {
                stbi_image_free(pixels);
                break;
            }
            AddToCache(filepath, pixels, width, height, channels, autoRotate, false, false, hasAlpha, decoder);
        }
    }
    if (SUCCEEDED(comHr)) CoUninitialize();
}

void StartPrefetchThread() {
    if (g_state.prefetchThread.joinable()) return;
    g_state.prefetchRunning = true;
    g_state.prefetchThread = std::thread(PrefetchThreadFunc);
}

void DisplaySize(int& w, int& h) {
    w = g_state.imageWidth;
    h = g_state.imageHeight;
    if (g_state.currentRotation == 90 || g_state.currentRotation == 270) std::swap(w, h);
}

void RotateImage(int degrees) {
    if (!g_state.imageData) return;
    g_state.currentRotation = (g_state.currentRotation + degrees + 360) % 360;
    if (g_state.hwnd) {
        RECT client{};
        GetClientRect(g_state.hwnd, &client);
        FitImageToWindow(client.right - client.left, client.bottom - client.top);
    }
    StoreCurrentInCache();
    ShowOSD(L"Imagen rotada");
    InvalidateRect(g_state.hwnd, nullptr, FALSE);
}

void FlipHorizontal() {
    if (!g_state.imageData) return;
    g_state.currentFlipH = !g_state.currentFlipH;
    StoreCurrentInCache();
    ShowOSD(g_state.currentFlipH ? L"Volteo horizontal activo" : L"Volteo horizontal normal");
    InvalidateRect(g_state.hwnd, nullptr, FALSE);
}

void FlipVertical() {
    if (!g_state.imageData) return;
    g_state.currentFlipV = !g_state.currentFlipV;
    StoreCurrentInCache();
    ShowOSD(g_state.currentFlipV ? L"Volteo vertical activo" : L"Volteo vertical normal");
    InvalidateRect(g_state.hwnd, nullptr, FALSE);
}

void ToggleUltraClarity() {
    if (!g_state.imageData) return;
    g_state.effectUltraClarity = !g_state.effectUltraClarity;
    ShowOSD(g_state.effectUltraClarity ? L"✨ Ultra-Claridad HDR: Activado" : L"Claridad estándar");
    InvalidateRect(g_state.hwnd, nullptr, FALSE);
}

void ToggleGrayscale() {
    if (!g_state.imageData) return;
    g_state.effectGrayscale = !g_state.effectGrayscale;
    ShowOSD(g_state.effectGrayscale ? L"Efecto: Escala de grises activado" : L"Efecto: Color normal");
    InvalidateRect(g_state.hwnd, nullptr, FALSE);
}

void ToggleInvert() {
    if (!g_state.imageData) return;
    g_state.effectInvert = !g_state.effectInvert;
    ShowOSD(g_state.effectInvert ? L"Efecto: Colores invertidos" : L"Efecto: Color normal");
    InvalidateRect(g_state.hwnd, nullptr, FALSE);
}

void EnsureImageVisible() {
    if (!g_state.imageData || !g_state.hwnd) return;
    RECT client{};
    GetClientRect(g_state.hwnd, &client);
    const int windowWidth = client.right - client.left;
    const int windowHeight = client.bottom - client.top;
    if (windowWidth <= 0 || windowHeight <= 0) return;
    int imageWidth = 0, imageHeight = 0;
    DisplaySize(imageWidth, imageHeight);
    const float drawW = imageWidth * g_state.zoom;
    const float drawH = imageHeight * g_state.zoom;
    if (drawW <= windowWidth) g_state.offsetX = (windowWidth - drawW) * 0.5f;
    else g_state.offsetX = std::min(0.0f, std::max(windowWidth - drawW, g_state.offsetX));
    if (drawH <= windowHeight) g_state.offsetY = (windowHeight - drawH) * 0.5f;
    else g_state.offsetY = std::min(0.0f, std::max(windowHeight - drawH, g_state.offsetY));
}

void FitImageToWindow(int windowWidth, int windowHeight) {
    if (!g_state.imageData || windowWidth <= 0 || windowHeight <= 0) return;
    int imageWidth = 0, imageHeight = 0;
    DisplaySize(imageWidth, imageHeight);
    const float scaleX = static_cast<float>(windowWidth) / static_cast<float>(imageWidth);
    const float scaleY = static_cast<float>(windowHeight) / static_cast<float>(imageHeight);
    g_state.zoom = std::max(MIN_ZOOM, std::min(MAX_ZOOM, std::min(scaleX, scaleY)));
    g_state.offsetX = (windowWidth - imageWidth * g_state.zoom) * 0.5f;
    g_state.offsetY = (windowHeight - imageHeight * g_state.zoom) * 0.5f;
    g_state.fitMode = true;
    EnsureImageVisible();
}

void ActualSize() {
    if (!g_state.imageData || !g_state.hwnd) return;
    RECT client{};
    GetClientRect(g_state.hwnd, &client);
    g_state.fitMode = false;
    g_state.zoom = 1.0f;
    int imageWidth = 0, imageHeight = 0;
    DisplaySize(imageWidth, imageHeight);
    g_state.offsetX = (client.right - imageWidth) * 0.5f;
    g_state.offsetY = (client.bottom - imageHeight) * 0.5f;
    EnsureImageVisible();
    ShowOSD(L"100% (Tamaño real)");
    InvalidateRect(g_state.hwnd, nullptr, FALSE);
}

void ZoomAt(float factor, int pivotX, int pivotY) {
    if (!g_state.imageData || !g_state.hwnd) return;
    const float oldZoom = g_state.zoom;
    const float newZoom = std::max(MIN_ZOOM, std::min(MAX_ZOOM, oldZoom * factor));
    if (newZoom == oldZoom) return;
    g_state.fitMode = false;
    const float imageX = (static_cast<float>(pivotX) - g_state.offsetX) / oldZoom;
    const float imageY = (static_cast<float>(pivotY) - g_state.offsetY) / oldZoom;
    g_state.zoom = newZoom;
    g_state.offsetX = static_cast<float>(pivotX) - imageX * g_state.zoom;
    g_state.offsetY = static_cast<float>(pivotY) - imageY * g_state.zoom;
    EnsureImageVisible();
    ShowOSD();
    InvalidateRect(g_state.hwnd, nullptr, FALSE);
}

// Utilidades de archivos
std::wstring GetFileSizeString(const std::wstring& filepath) {
    WIN32_FILE_ATTRIBUTE_DATA fileData{};
    if (!GetFileAttributesExW(filepath.c_str(), GetFileExInfoStandard, &fileData)) return L"?";
    LARGE_INTEGER size{};
    size.HighPart = fileData.nFileSizeHigh;
    size.LowPart = fileData.nFileSizeLow;
    const wchar_t* units[] = { L"B", L"KB", L"MB", L"GB" };
    int unit = 0;
    double value = static_cast<double>(size.QuadPart);
    while (value >= 1024.0 && unit < 3) { value /= 1024.0; ++unit; }
    wchar_t buffer[32];
    swprintf_s(buffer, L"%.1f %s", value, units[unit]);
    return buffer;
}

std::wstring GetFileName(const std::wstring& filepath) {
    const size_t pos = filepath.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return filepath;
    return filepath.substr(pos + 1);
}

void AddRoundedRect(GraphicsPath& path, const RectF& rect, float radius) {
    float diameter = radius * 2.0f;
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180, 90);
    path.AddArc(rect.X + rect.Width - diameter, rect.Y, diameter, diameter, 270, 90);
    path.AddArc(rect.X + rect.Width - diameter, rect.Y + rect.Height - diameter, diameter, diameter, 0, 90);
    path.AddArc(rect.X, rect.Y + rect.Height - diameter, diameter, diameter, 90, 90);
    path.CloseFigure();
}

void LayoutHud(const RECT& client) {
    g_state.hudCount = 0;
    const int dockHeight = 44, itemH = 32, gap = 6, paddingX = 10, paddingY = 6;

    struct ItemDef { HudId id; const wchar_t* label; int w; } items[] = {
        { HUD_PREV, L"◀", 32 }, { HUD_NEXT, L"▶", 32 }, { HUD_FIT, L"Ajustar", 60 },
        { HUD_ONE, L"1:1", 44 }, { HUD_CLARITY, L"✨ Claridad", 74 }, { HUD_ROT, L"Rotar", 50 },
        { HUD_FLIP, L"Voltear", 56 }, { HUD_WALLPAPER, L"Fondo", 52 }, { HUD_SAVE, L"Guardar", 60 },
        { HUD_FULL, g_state.isFullscreen ? L"Ventana" : L"Pantalla", 68 }, { HUD_OPEN, L"Abrir", 50 }
    };

    int totalItemsWidth = 0;
    const int count = sizeof(items) / sizeof(items[0]);
    for (int i = 0; i < count; ++i) totalItemsWidth += items[i].w + (i > 0 ? gap : 0);

    const int dockWidth = totalItemsWidth + (paddingX * 2);
    const int dockX = std::max(10, static_cast<int>((client.right - dockWidth) / 2));
    const int dockY = client.bottom - dockHeight - 16;

    g_state.dockRect = { dockX, dockY, dockX + dockWidth, dockY + dockHeight };

    int curX = dockX + paddingX;
    const int curY = dockY + paddingY;

    for (int i = 0; i < count && g_state.hudCount < 12; ++i) {
        HudItem& item = g_state.hud[g_state.hudCount++];
        item.id = items[i].id; item.label = items[i].label;
        item.rc = { curX, curY, curX + items[i].w, curY + itemH };
        curX += items[i].w + gap;
    }
}

HudId HitTestHud(int x, int y) {
    if (!g_state.hwnd) return HUD_NONE;
    RECT client{};
    GetClientRect(g_state.hwnd, &client);
    LayoutHud(client);

    for (int i = 0; i < g_state.hudCount; ++i) {
        if (PtInRect(&g_state.hud[i].rc, POINT{ x, y })) return g_state.hud[i].id;
    }
    return HUD_NONE;
}

void RenderHud(Graphics& graphics, const RECT& client) {
    LayoutHud(client);

    // Auto-hide dock logic
    bool shouldShowDock = true;
    if (g_state.dockAutoHide) {
        DWORD now = GetTickCount();
        if (now - g_state.dockLastActivity > DOCK_HIDE_MS) {
            shouldShowDock = false;
        }
    }

    // 1. Mensaje OSD / Estado Flotante
    if (!g_state.statusMessage.empty() &&
        (g_state.osdPinned || GetTickCount() - g_state.osdDisplayTime < OSD_MS)) {
        FontFamily fontFamily(L"Segoe UI");
        Font font(&fontFamily, 10.5f, FontStyleBold, UnitPoint);
        RectF layoutRect(0, 0, 600, 40);
        RectF boundRect;
        StringFormat format;
        format.SetAlignment(StringAlignmentCenter);
        format.SetLineAlignment(StringAlignmentCenter);
        graphics.MeasureString(g_state.statusMessage.c_str(), -1, &font, layoutRect, &format, &boundRect);

        const float osdW = boundRect.Width + 30.0f;
        const float osdH = 32.0f;
        const float osdX = (client.right - osdW) / 2.0f;
        const float osdY = 14.0f;

        RectF osdRect(osdX, osdY, osdW, osdH);
        GraphicsPath osdPath;
        AddRoundedRect(osdPath, osdRect, 16.0f);

        SolidBrush osdBg(Color(220, 12, 14, 20));
        Pen osdBorder(Color(130, 200, 210, 230), 1.2f);
        graphics.FillPath(&osdBg, &osdPath);
        graphics.DrawPath(&osdBorder, &osdPath);

        SolidBrush textBrush(Color(255, 255, 255));
        graphics.DrawString(g_state.statusMessage.c_str(), -1, &font, osdRect, &format, &textBrush);
    }

    // 2. Dock Flotante Acrílico Inferior
    if (shouldShowDock) {
        RectF dockRectF(static_cast<float>(g_state.dockRect.left),
                       static_cast<float>(g_state.dockRect.top),
                       static_cast<float>(g_state.dockRect.right - g_state.dockRect.left),
                       static_cast<float>(g_state.dockRect.bottom - g_state.dockRect.top));

        GraphicsPath dockPath;
        AddRoundedRect(dockPath, dockRectF, 22.0f);

        // Sombra
        RectF shadowRect = dockRectF;
        shadowRect.Y += 5.0f;
        GraphicsPath shadowPath;
        AddRoundedRect(shadowPath, shadowRect, 22.0f);
        SolidBrush shadowBrush(GLASS_DOCK_SHADOW);
        graphics.FillPath(&shadowBrush, &shadowPath);

        // Fondo y borde
        SolidBrush dockBg(GLASS_DOCK_BG);
        Pen dockBorder(GLASS_DOCK_BORDER, 1.2f);
        graphics.FillPath(&dockBg, &dockPath);
        graphics.DrawPath(&dockBorder, &dockPath);

        // Botones
        FontFamily btnFontFamily(L"Segoe UI");
        Font btnFont(&btnFontFamily, 9.5f, FontStyleBold, UnitPoint);
        StringFormat btnFormat;
        btnFormat.SetAlignment(StringAlignmentCenter);
        btnFormat.SetLineAlignment(StringAlignmentCenter);

        for (int i = 0; i < g_state.hudCount; ++i) {
            const bool hot = (g_state.hud[i].id == g_state.hudHot);
            const bool active = (g_state.hud[i].id == HUD_CLARITY && g_state.effectUltraClarity);

            RectF itemRect(static_cast<float>(g_state.hud[i].rc.left),
                          static_cast<float>(g_state.hud[i].rc.top),
                          static_cast<float>(g_state.hud[i].rc.right - g_state.hud[i].rc.left),
                          static_cast<float>(g_state.hud[i].rc.bottom - g_state.hud[i].rc.top));

            GraphicsPath itemPath;
            AddRoundedRect(itemPath, itemRect, 15.0f);

            if (active) {
                SolidBrush activeBrush(GLASS_BTN_ACTIVE);
                Pen activeBorder(Color(255, 80, 240, 180), 1.2f);
                graphics.FillPath(&activeBrush, &itemPath);
                graphics.DrawPath(&activeBorder, &itemPath);
            } else if (hot) {
                SolidBrush hotBrush(GLASS_BTN_HOT);
                Pen hotBorder(GLASS_BTN_BORDER_HOT, 1.2f);
                graphics.FillPath(&hotBrush, &itemPath);
                graphics.DrawPath(&hotBorder, &itemPath);
            } else {
                SolidBrush normalBrush(GLASS_BTN_NORMAL);
                Pen normalBorder(GLASS_BTN_BORDER_NORMAL, 1.0f);
                graphics.FillPath(&normalBrush, &itemPath);
                graphics.DrawPath(&normalBorder, &itemPath);
            }

            SolidBrush btnTextBrush((hot || active) ? Color(255, 255, 255) : Color(225, 230, 238));
            graphics.DrawString(g_state.hud[i].label, -1, &btnFont, itemRect, &btnFormat, &btnTextBrush);
        }
    }
}

void CreateDoubleBuffer(int width, int height) {
    FreeDoubleBuffer();
    if (width <= 0 || height <= 0 || !g_state.hwnd) return;
    HDC hdc = GetDC(g_state.hwnd);
    if (!hdc) return;
    g_state.hdcMem = CreateCompatibleDC(hdc);
    if (!g_state.hdcMem) {
        ReleaseDC(g_state.hwnd, hdc);
        return;
    }
    g_state.hbmMem = CreateCompatibleBitmap(hdc, width, height);
    if (!g_state.hbmMem) {
        DeleteDC(g_state.hdcMem);
        g_state.hdcMem = nullptr;
        ReleaseDC(g_state.hwnd, hdc);
        return;
    }
    g_state.hbmOld = static_cast<HBITMAP>(SelectObject(g_state.hdcMem, g_state.hbmMem));
    ReleaseDC(g_state.hwnd, hdc);
}

void RenderEmptyState(Graphics& graphics, const RECT& clientRect) {
    SolidBrush bgBrush(Color(static_cast<BYTE>(255), static_cast<BYTE>(GetRValue(BG_COLOR)), static_cast<BYTE>(GetGValue(BG_COLOR)), static_cast<BYTE>(GetBValue(BG_COLOR))));
    graphics.FillRectangle(&bgBrush, 0, 0, clientRect.right, clientRect.bottom);

    FontFamily titleFamily(L"Segoe UI");
    Font titleFont(&titleFamily, 28.0f, FontStyleBold, UnitPoint);
    Font hintFont(&titleFamily, 11.5f, FontStyleRegular, UnitPoint);

    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);

    RectF titleRect(0.0f, clientRect.bottom * 0.38f - 30.0f, static_cast<float>(clientRect.right), 50.0f);
    RectF hintRect(0.0f, clientRect.bottom * 0.38f + 25.0f, static_cast<float>(clientRect.right), 40.0f);

    SolidBrush titleBrush(Color(245, 248, 252));
    SolidBrush hintBrush(Color(160, 170, 185));

    graphics.DrawString(L"ARTPICST", -1, &titleFont, titleRect, &format, &titleBrush);
    graphics.DrawString(L"Arrastra una imagen aquí  ·  Ctrl + O abrir  ·  F11 pantalla completa  ·  F1 info/atajos",
                        -1, &hintFont, hintRect, &format, &hintBrush);
}

void DrawCheckerboard(Graphics& g, float x, float y, float w, float h) {
    const float tileSize = 16.0f;
    SolidBrush brushA(Color(255, GetRValue(CHECKER_A), GetGValue(CHECKER_A), GetBValue(CHECKER_A)));
    SolidBrush brushB(Color(255, GetRValue(CHECKER_B), GetGValue(CHECKER_B), GetBValue(CHECKER_B)));

    g.FillRectangle(&brushA, x, y, w, h);
    int cols = static_cast<int>(std::ceil(w / tileSize));
    int rows = static_cast<int>(std::ceil(h / tileSize));
    for (int r = 0; r < rows; ++r) {
        for (int c = (r % 2); c < cols; c += 2) {
            float tx = x + c * tileSize, ty = y + r * tileSize;
            float tw = std::min(tileSize, x + w - tx), th = std::min(tileSize, y + h - ty);
            if (tw > 0 && th > 0) g.FillRectangle(&brushB, tx, ty, tw, th);
        }
    }
}

void RenderImage() {
    if (!g_state.hdcMem || !g_state.hwnd) return;
    RECT rect{};
    GetClientRect(g_state.hwnd, &rect);

    Graphics graphics(g_state.hdcMem);
    
    // Configuración de calidad MÁXIMA de renderizado
    graphics.SetCompositingMode(CompositingModeSourceOver);
    graphics.SetCompositingQuality(CompositingQualityAssumeLinear); // Máxima calidad de composición
    graphics.SetSmoothingMode(SmoothingModeAntiAlias); // Anti-aliasing de alta calidad
    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality); // Precisión de píxel máxima
    graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit); // Texto de alta claridad

    if (!g_state.imageData) {
        RenderEmptyState(graphics, rect);
        RenderHud(graphics, rect);
        return;
    }

    SolidBrush bgBrush(Color(255, GetRValue(BG_COLOR), GetGValue(BG_COLOR), GetBValue(BG_COLOR)));
    graphics.FillRectangle(&bgBrush, 0, 0, rect.right, rect.bottom);

    Bitmap bitmap(g_state.imageWidth, g_state.imageHeight,
                  g_state.imageWidth * 4, PixelFormat32bppARGB, g_state.imageData);

    // Selección inteligente de modo de interpolación para máxima calidad
    const bool pixelPerfectZoom = std::fabs(g_state.zoom - std::round(g_state.zoom)) < 0.01f && g_state.zoom >= 1.0f;
    
    if (pixelPerfectZoom) {
        graphics.SetInterpolationMode(InterpolationModeNearestNeighbor);
    } else if (g_state.zoom > 1.0f) {
        // Para zoom > 1.0, usar interpolación bicúbica de alta calidad
        graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    } else if (g_state.zoom < 0.5f) {
        // Para zoom muy pequeño, usar interpolación de alta calidad con suavizado
        graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    } else {
        // Para zoom normal, usar interpolación de alta calidad
        graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    }
    
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);

    int boxW = 0, boxH = 0;
    DisplaySize(boxW, boxH);
    const float destW = boxW * g_state.zoom;
    const float destH = boxH * g_state.zoom;
    const float centerX = g_state.offsetX + destW * 0.5f;
    const float centerY = g_state.offsetY + destH * 0.5f;

    if (g_state.hasAlpha) {
        GraphicsState bgState = graphics.Save();
        graphics.TranslateTransform(centerX, centerY);
        graphics.RotateTransform(static_cast<REAL>(g_state.currentRotation));
        graphics.TranslateTransform(-g_state.imageWidth * g_state.zoom * 0.5f,
                                    -g_state.imageHeight * g_state.zoom * 0.5f);
        DrawCheckerboard(graphics, 0.0f, 0.0f, g_state.imageWidth * g_state.zoom, g_state.imageHeight * g_state.zoom);
        graphics.Restore(bgState);
    }

    ImageAttributes imgAttr;
    imgAttr.SetWrapMode(WrapModeClamp);

    // Aplicar mejora automática de calidad si está habilitada
    if (ENABLE_ULTRA_QUALITY_RENDERING && !g_state.effectGrayscale && !g_state.effectInvert && !g_state.effectUltraClarity) {
        // Matriz de mejora automática (contraste sutil + claridad)
        const float c = 1.03f; // Contraste mejorado
        const float t = (1.0f - c) / 2.0f;
        ColorMatrix autoEnhanceMatrix = {
            c,     0.0f,  0.0f,  0.0f, 0.0f,
            0.0f,  c,     0.0f,  0.0f, 0.0f,
            0.0f,  0.0f,  c,     0.0f, 0.0f,
            0.0f,  0.0f,  0.0f,  1.0f, 0.0f,
            t,     t,     t,     0.0f, 1.0f
        };
        imgAttr.SetColorMatrix(&autoEnhanceMatrix, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
    }

    if (g_state.effectGrayscale) {
        ColorMatrix grayMatrix = {
            0.299f, 0.299f, 0.299f, 0.0f, 0.0f,
            0.587f, 0.587f, 0.587f, 0.0f, 0.0f,
            0.114f, 0.114f, 0.114f, 0.0f, 0.0f,
            0.0f,   0.0f,   0.0f,   1.0f, 0.0f,
            0.0f,   0.0f,   0.0f,   0.0f, 1.0f
        };
        imgAttr.SetColorMatrix(&grayMatrix, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
    } else if (g_state.effectInvert) {
        ColorMatrix invMatrix = {
            -1.0f,  0.0f,  0.0f, 0.0f, 0.0f,
             0.0f, -1.0f,  0.0f, 0.0f, 0.0f,
             0.0f,  0.0f, -1.0f, 0.0f, 0.0f,
             0.0f,  0.0f,  0.0f, 1.0f, 0.0f,
             1.0f,  1.0f,  1.0f, 0.0f, 1.0f
        };
        imgAttr.SetColorMatrix(&invMatrix, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
    } else if (g_state.effectUltraClarity) {
        // Matriz Ultra-Claridad HDR MEJORADA (Realce de micro-contraste y detalles finos)
        const float c = 1.10f; // Aumentado para mayor claridad
        const float t = (1.0f - c) / 2.0f;
        ColorMatrix clarityMatrix = {
            c,     0.0f,  0.0f,  0.0f, 0.0f,
            0.0f,  c,     0.0f,  0.0f, 0.0f,
            0.0f,  0.0f,  c,     0.0f, 0.0f,
            0.0f,  0.0f,  0.0f,  1.0f, 0.0f,
            t,     t,     t,     0.0f, 1.0f
        };
        imgAttr.SetColorMatrix(&clarityMatrix, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
    }

    GraphicsState state = graphics.Save();
    graphics.TranslateTransform(centerX, centerY);
    graphics.RotateTransform(static_cast<REAL>(g_state.currentRotation));
    if (g_state.currentFlipH || g_state.currentFlipV) {
        graphics.ScaleTransform(g_state.currentFlipH ? -1.0f : 1.0f,
                                g_state.currentFlipV ? -1.0f : 1.0f);
    }
    graphics.TranslateTransform(-g_state.imageWidth * g_state.zoom * 0.5f,
                                -g_state.imageHeight * g_state.zoom * 0.5f);

    graphics.DrawImage(&bitmap,
                       RectF(0.0f, 0.0f, g_state.imageWidth * g_state.zoom, g_state.imageHeight * g_state.zoom),
                       0.0f, 0.0f, static_cast<REAL>(g_state.imageWidth), static_cast<REAL>(g_state.imageHeight),
                       UnitPixel, &imgAttr);

    graphics.Restore(state);

    // Renderizar Dock y controles flotantes con transparencia acrílica
    RenderHud(graphics, rect);
}

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    auto* pCodec = static_cast<ImageCodecInfo*>(malloc(size));
    if (!pCodec) return -1;
    GetImageEncoders(num, size, pCodec);
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pCodec[j].MimeType, format) == 0) {
            *pClsid = pCodec[j].Clsid;
            free(pCodec);
            return j;
        }
    }
    free(pCodec);
    return -1;
}

bool SaveImageDialog(HWND hwnd) {
    if (!g_state.imageData) return false;
    ComPtr<IFileSaveDialog> dialog;
    HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog.p));
    if (FAILED(hr) || !dialog) return false;

    COMDLG_FILTERSPEC filters[] = {
        { L"PNG Image (*.png)", L"*.png" },
        { L"JPEG Image (*.jpg)", L"*.jpg;*.jpeg" },
        { L"BMP Image (*.bmp)", L"*.bmp" }
    };
    dialog->SetFileTypes(3, filters);
    dialog->SetDefaultExtension(L"png");
    std::wstring defName = GetFileName(g_state.currentFilePath);
    if (!defName.empty()) {
        size_t dot = defName.find_last_of(L'.');
        if (dot != std::wstring::npos) defName = defName.substr(0, dot);
        defName += L"_export.png";
        dialog->SetFileName(defName.c_str());
    }
    dialog->SetTitle(L"Guardar imagen como");
    if (FAILED(dialog->Show(hwnd))) return false;

    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(&item)) || !item) return false;
    PWSTR savePath = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &savePath)) || !savePath) return false;

    std::wstring outPath = savePath;
    CoTaskMemFree(savePath);

    int boxW = 0, boxH = 0;
    DisplaySize(boxW, boxH);
    Bitmap source(g_state.imageWidth, g_state.imageHeight,
                  g_state.imageWidth * 4, PixelFormat32bppARGB, g_state.imageData);
    Bitmap output(boxW, boxH, PixelFormat32bppARGB);
    Graphics g(&output);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.TranslateTransform(static_cast<REAL>(boxW) * 0.5f, static_cast<REAL>(boxH) * 0.5f);
    g.RotateTransform(static_cast<REAL>(g_state.currentRotation));
    if (g_state.currentFlipH || g_state.currentFlipV) {
        g.ScaleTransform(g_state.currentFlipH ? -1.0f : 1.0f,
                         g_state.currentFlipV ? -1.0f : 1.0f);
    }
    g.TranslateTransform(-static_cast<REAL>(g_state.imageWidth) * 0.5f,
                         -static_cast<REAL>(g_state.imageHeight) * 0.5f);
    g.DrawImage(&source, 0, 0, g_state.imageWidth, g_state.imageHeight);

    std::wstring ext = GetExtensionLower(outPath);
    CLSID encoderClsid{};
    if (ext == L"jpg" || ext == L"jpeg") {
        GetEncoderClsid(L"image/jpeg", &encoderClsid);
    } else if (ext == L"bmp") {
        GetEncoderClsid(L"image/bmp", &encoderClsid);
    } else {
        GetEncoderClsid(L"image/png", &encoderClsid);
    }

    if (output.Save(outPath.c_str(), &encoderClsid, nullptr) == Ok) {
        ShowOSD(L"Imagen guardada con éxito");
        return true;
    } else {
        ShowOSD(L"Error al guardar la imagen");
        return false;
    }
}

void SetAsWallpaper() {
    if (g_state.currentFilePath.empty()) return;
    BOOL res = SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, const_cast<wchar_t*>(g_state.currentFilePath.c_str()),
                                     SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    if (res) {
        ShowOSD(L"Fondo de pantalla actualizado");
    } else {
        ShowOSD(L"No se pudo establecer el fondo");
    }
}

void ShowExifDialog(HWND hwnd) {
    if (!g_state.imageData || g_state.currentFilePath.empty()) return;
    int dw = 0, dh = 0;
    DisplaySize(dw, dh);
    double megapixels = (static_cast<double>(g_state.imageWidth) * g_state.imageHeight) / 1000000.0;
    std::wstring info = L"Propiedades y Metadatos de la Imagen:\n\n";
    info += L"• Archivo: " + GetFileName(g_state.currentFilePath) + L"\n";
    info += L"• Ruta: " + g_state.currentFilePath + L"\n";
    info += L"• Resolución nativa: " + std::to_wstring(g_state.imageWidth) + L" x " + std::to_wstring(g_state.imageHeight) + L" px\n";
    wchar_t mpBuf[64];
    swprintf_s(mpBuf, L"• Megapíxeles: %.2f MP\n", megapixels);
    info += mpBuf;
    info += L"• Tamaño en disco: " + GetFileSizeString(g_state.currentFilePath) + L"\n";
    info += L"• Motor de carga: " + g_state.decoderName + (g_state.hasAlpha ? L" (con transparencia alfa)" : L" (opaca)") + L"\n";
    info += L"• Rotación / Transformación: " + std::to_wstring(g_state.currentRotation) + L"°" +
            (g_state.currentFlipH ? L" [Volteo H]" : L"") + (g_state.currentFlipV ? L" [Volteo V]" : L"") + L"\n";
    wchar_t zoomBuf[64];
    swprintf_s(zoomBuf, L"• Zoom actual en pantalla: %.1f%%\n", g_state.zoom * 100.0f);
    info += zoomBuf;
    ShowThemedMessageBox(hwnd, L"Metadatos de Imagen — ARTPICST", info.c_str(), MB_OK, MB_ICONINFORMATION);
}

bool CopyPathToClipboard() {
    if (g_state.currentFilePath.empty()) return false;
    if (!OpenClipboard(g_state.hwnd)) return false;
    EmptyClipboard();
    const size_t bytes = (g_state.currentFilePath.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) {
        CloseClipboard();
        return false;
    }
    wchar_t* dest = static_cast<wchar_t*>(GlobalLock(mem));
    if (!dest) {
        GlobalFree(mem);
        CloseClipboard();
        return false;
    }
    wcscpy_s(dest, g_state.currentFilePath.size() + 1, g_state.currentFilePath.c_str());
    GlobalUnlock(mem);
    SetClipboardData(CF_UNICODETEXT, mem);
    CloseClipboard();
    ShowOSD(L"Ruta copiada");
    InvalidateRect(g_state.hwnd, nullptr, FALSE);
    return true;
}

bool CopyImageToClipboard() {
    if (!g_state.imageData) return false;
    Bitmap source(g_state.imageWidth, g_state.imageHeight,
                  g_state.imageWidth * 4, PixelFormat32bppARGB, g_state.imageData);
    int boxW = 0, boxH = 0;
    DisplaySize(boxW, boxH);
    Bitmap* output = &source;
    Bitmap rotated(boxW, boxH, PixelFormat32bppARGB);
    if (g_state.currentRotation != 0 || g_state.currentFlipH || g_state.currentFlipV) {
        if (rotated.GetLastStatus() != Ok) return false;
        Graphics g(&rotated);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.TranslateTransform(static_cast<REAL>(boxW) * 0.5f, static_cast<REAL>(boxH) * 0.5f);
        g.RotateTransform(static_cast<REAL>(g_state.currentRotation));
        if (g_state.currentFlipH || g_state.currentFlipV) {
            g.ScaleTransform(g_state.currentFlipH ? -1.0f : 1.0f,
                             g_state.currentFlipV ? -1.0f : 1.0f);
        }
        g.TranslateTransform(-static_cast<REAL>(g_state.imageWidth) * 0.5f,
                             -static_cast<REAL>(g_state.imageHeight) * 0.5f);
        g.DrawImage(&source, 0, 0, g_state.imageWidth, g_state.imageHeight);
        output = &rotated;
    }
    HBITMAP hBitmap = nullptr;
    if (output->GetHBITMAP(Color(0, 0, 0, 0), &hBitmap) != Ok || !hBitmap) return false;
    if (!OpenClipboard(g_state.hwnd)) {
        DeleteObject(hBitmap);
        return false;
    }
    EmptyClipboard();
    SetClipboardData(CF_BITMAP, hBitmap);
    CloseClipboard();
    ShowOSD(L"Imagen copiada al portapapeles");
    InvalidateRect(g_state.hwnd, nullptr, FALSE);
    return true;
}

void ApplyWindowMode(HWND hwnd, WindowMode mode) {
    if (!hwnd) return;
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(monitor, &mi);

    if (mode == WindowMode::Fullscreen) {
        if (!g_state.isFullscreen) {
            g_state.windowedStyle = GetWindowLongW(hwnd, GWL_STYLE);
            g_state.windowedExStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
            g_state.windowedPlacement.length = sizeof(WINDOWPLACEMENT);
            GetWindowPlacement(hwnd, &g_state.windowedPlacement);
        }
        SetWindowLongW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
        SetWindowPos(hwnd, HWND_TOP,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        g_state.isFullscreen = true;
        g_state.windowMode = WindowMode::Fullscreen;
        return;
    }

    LONG style = WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    if (g_state.windowedStyle) style = g_state.windowedStyle | WS_VISIBLE;
    style |= WS_OVERLAPPEDWINDOW;
    style &= ~WS_POPUP;
    SetWindowLongW(hwnd, GWL_STYLE, style);
    if (g_state.windowedExStyle) {
        SetWindowLongW(hwnd, GWL_EXSTYLE, g_state.windowedExStyle);
    }
    EnableDarkTitleBar(hwnd);
    g_state.isFullscreen = false;
    g_state.windowMode = mode;

    if (g_state.windowedPlacement.length &&
        (g_state.windowedPlacement.rcNormalPosition.right > g_state.windowedPlacement.rcNormalPosition.left)) {
        SetWindowPlacement(hwnd, &g_state.windowedPlacement);
        if (mode == WindowMode::Maximized) ShowWindow(hwnd, SW_MAXIMIZE);
        else ShowWindow(hwnd, SW_RESTORE);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        return;
    }

    const int monitorW = mi.rcWork.right - mi.rcWork.left;
    const int monitorH = mi.rcWork.bottom - mi.rcWork.top;
    if (mode == WindowMode::Maximized) {
        ShowWindow(hwnd, SW_SHOWMAXIMIZED);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        return;
    }
    int width = std::min(std::max(1100, monitorW - 160), monitorW);
    int height = std::min(std::max(720, monitorH - 140), monitorH);
    const int x = mi.rcWork.left + (monitorW - width) / 2;
    const int y = mi.rcWork.top + (monitorH - height) / 2;
    SetWindowPos(hwnd, HWND_TOP, x, y, width, height, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

void ToggleFullscreen() {
    if (!g_state.hwnd) return;
    if (!g_state.isFullscreen) ApplyWindowMode(g_state.hwnd, WindowMode::Fullscreen);
    else ApplyWindowMode(g_state.hwnd, WindowMode::Normal);
    ShowOSD(g_state.isFullscreen ? L"Pantalla completa" : L"Modo ventana");
    InvalidateRect(g_state.hwnd, nullptr, FALSE);
}

void ToggleSlideshow() {
    g_state.isSlideshowActive = !g_state.isSlideshowActive;
    if (g_state.isSlideshowActive) {
        SetTimer(g_state.hwnd, TIMER_SLIDESHOW, SLIDESHOW_INTERVAL_MS, nullptr);
        ShowOSD(L"Presentación iniciada (F5 para salir)");
    } else {
        KillTimer(g_state.hwnd, TIMER_SLIDESHOW);
        ShowOSD(L"Presentación detenida");
    }
}

void NextImage() {
    const size_t count = ImageCount();
    if (count == 0) return;
    const size_t start = g_state.currentImageIndex;
    for (size_t step = 1; step <= count; ++step) {
        const size_t idx = (start + step) % count;
        if (LoadImageByIndex(idx)) {
            RequestPrefetch(idx);
            InvalidateRect(g_state.hwnd, nullptr, FALSE);
            return;
        }
    }
}

void PreviousImage() {
    const size_t count = ImageCount();
    if (count == 0) return;
    const size_t start = g_state.currentImageIndex;
    for (size_t step = 1; step <= count; ++step) {
        const size_t idx = (start + count - step) % count;
        if (LoadImageByIndex(idx)) {
            RequestPrefetch(idx);
            InvalidateRect(g_state.hwnd, nullptr, FALSE);
            return;
        }
    }
}

void JumpTo(size_t index) {
    if (LoadImageByIndex(index)) {
        RequestPrefetch(index);
        InvalidateRect(g_state.hwnd, nullptr, FALSE);
    }
}

void OpenPath(const std::wstring& path) {
    const std::wstring resolved = NormalizePath(path);
    DWORD attrs = GetFileAttributesW(resolved.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        HandleError(L"No se pudo abrir la ruta.", false);
        return;
    }
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
        ScanFolderForImages(resolved);
        if (ImageCount() == 0) {
            ShowOSD(L"No hay imágenes en la carpeta");
            UpdateWindowTitle();
            InvalidateRect(g_state.hwnd, nullptr, FALSE);
            return;
        }
        LoadImageByIndex(0);
        RequestPrefetch(0);
        InvalidateRect(g_state.hwnd, nullptr, FALSE);
        return;
    }
    size_t slash = resolved.find_last_of(L"\\/");
    std::wstring folder = (slash == std::wstring::npos) ? L"." : resolved.substr(0, slash);
    ScanFolderForImages(folder);
    size_t idx = 0;
    if (FindImageIndex(resolved, idx)) {
        LoadImageByIndex(idx);
        RequestPrefetch(idx);
    } else if (LoadImageFromPath(resolved)) {
        EnsureFileInList(resolved);
    } else if (ImageCount() > 0) {
        LoadImageByIndex(0);
        RequestPrefetch(0);
    }
    InvalidateRect(g_state.hwnd, nullptr, FALSE);
}

void OpenInExplorer() {
    if (g_state.currentFilePath.empty()) return;
    std::wstring param = L"/select,\"" + g_state.currentFilePath + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", param.c_str(), nullptr, SW_SHOWNORMAL);
}

void DeleteCurrentImage() {
    if (g_state.currentFilePath.empty()) return;

    const std::wstring path = g_state.currentFilePath;
    const std::wstring name = GetFileName(path);
    std::wstring prompt = L"¿Desea enviar \"" + name + L"\" a la Papelera de reciclaje?";
    if (ShowThemedMessageBox(g_state.hwnd, L"Eliminar imagen", prompt.c_str(), MB_YESNO, MB_ICONQUESTION) != IDYES) {
        return;
    }

    std::vector<wchar_t> fromBuf(path.size() + 2, L'\0');
    memcpy(fromBuf.data(), path.c_str(), path.size() * sizeof(wchar_t));

    SHFILEOPSTRUCTW fileOp{};
    fileOp.hwnd = g_state.hwnd;
    fileOp.wFunc = FO_DELETE;
    fileOp.pFrom = fromBuf.data();
    fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMMKDIR | FOF_SILENT;

    int result = SHFileOperationW(&fileOp);
    if (result == 0 && !fileOp.fAnyOperationsAborted) {
        ShowOSD(L"Imagen enviada a la Papelera");
        std::lock_guard<std::mutex> lock(g_state.filesMutex);
        auto it = std::find_if(g_state.imageFiles.begin(), g_state.imageFiles.end(),
                               [&path](const std::wstring& f) { return PathsEqualCaseInsensitive(f, path); });
        if (it != g_state.imageFiles.end()) {
            size_t idx = std::distance(g_state.imageFiles.begin(), it);
            g_state.imageFiles.erase(it);
            if (g_state.imageFiles.empty()) {
                FreeCurrentImage();
                UpdateWindowTitle();
            } else {
                if (idx >= g_state.imageFiles.size()) idx = g_state.imageFiles.size() - 1;
                g_state.currentImageIndex = idx;
                std::wstring nextPath = g_state.imageFiles[idx];
                LoadImageFromPath(nextPath);
            }
        }
        InvalidateRect(g_state.hwnd, nullptr, FALSE);
    } else {
        ShowOSD(L"No se pudo eliminar el archivo");
    }
}

std::wstring GetExecutablePath() {
    wchar_t path[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return {};
    return path;
}

std::wstring GetExecutableFolder() {
    std::wstring full = GetExecutablePath();
    size_t pos = full.find_last_of(L'\\');
    if (pos == std::wstring::npos) return {};
    return full.substr(0, pos);
}

bool OpenImageFileDialog(HWND hwnd) {
    ComPtr<IFileOpenDialog> dialog;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog.p));
    if (FAILED(hr) || !dialog) return false;
    COMDLG_FILTERSPEC filters[] = {
        { L"Todas las imágenes soportadas", L"*.jpg;*.jpeg;*.jpe;*.jfif;*.png;*.bmp;*.gif;*.tif;*.tiff;*.webp;*.heic;*.heif;*.avif;*.jxl;*.jxr;*.wdp;*.ico;*.cur;*.tga;*.psd;*.hdr;*.jp2;*.dng;*.cr2;*.nef;*.arw;*.raw;*.emf;*.wmf" },
        { L"Todos los archivos (*.*)", L"*.*" }
    };
    dialog->SetFileTypes(2, filters);
    dialog->SetTitle(L"Abrir imagen");
    FILEOPENDIALOGOPTIONS opts = 0;
    dialog->GetOptions(&opts);
    dialog->SetOptions(opts | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM);
    
    // Apply dark theme to COM dialog
    IFileDialogCustomize* customize = nullptr;
    if (SUCCEEDED(dialog->QueryInterface(IID_PPV_ARGS(&customize)))) {
        DWORD dwFlags = 0;
        if (SUCCEEDED(dialog->GetOptions(&dwFlags))) {
            dialog->SetOptions(dwFlags | FOS_DONTADDTORECENT);
        }
        customize->Release();
    }
    
    hr = dialog->Show(hwnd);
    if (FAILED(hr)) return false;
    ComPtr<IShellItem> item;
    hr = dialog->GetResult(&item);
    if (FAILED(hr) || !item) return false;
    PWSTR path = nullptr;
    hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
    if (FAILED(hr) || !path) return false;
    OpenPath(path);
    CoTaskMemFree(path);
    return true;
}

bool OpenFolderDialog(HWND hwnd) {
    ComPtr<IFileOpenDialog> dialog;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog.p));
    if (FAILED(hr) || !dialog) return false;
    dialog->SetTitle(L"Abrir carpeta");
    FILEOPENDIALOGOPTIONS opts = 0;
    dialog->GetOptions(&opts);
    dialog->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    hr = dialog->Show(hwnd);
    if (FAILED(hr)) return false;
    ComPtr<IShellItem> item;
    hr = dialog->GetResult(&item);
    if (FAILED(hr) || !item) return false;
    PWSTR path = nullptr;
    hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
    if (FAILED(hr) || !path) return false;
    OpenPath(path);
    CoTaskMemFree(path);
    return true;
}

void InvokeHud(HudId id) {
    switch (id) {
        case HUD_PREV: PreviousImage(); break;
        case HUD_NEXT: NextImage(); break;
        case HUD_FIT:
            if (g_state.hwnd && g_state.imageData) {
                RECT rect{};
                GetClientRect(g_state.hwnd, &rect);
                FitImageToWindow(rect.right, rect.bottom);
                ShowOSD(L"Ajuste perfecto");
                InvalidateRect(g_state.hwnd, nullptr, FALSE);
            }
            break;
        case HUD_ONE: ActualSize(); break;
        case HUD_CLARITY: ToggleUltraClarity(); break;
        case HUD_ROT: RotateImage(90); break;
        case HUD_FLIP: FlipHorizontal(); break;
        case HUD_WALLPAPER: SetAsWallpaper(); break;
        case HUD_SAVE: SaveImageDialog(g_state.hwnd); break;
        case HUD_FULL: ToggleFullscreen(); break;
        case HUD_OPEN: OpenImageFileDialog(g_state.hwnd); break;
        default: break;
    }
}

void ShowContextMenu(HWND hwnd, int x, int y) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 23, L"Info (Controles, Atajos y Funciones)\tF1");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 1, L"Abrir imagen...\tCtrl + O");
    AppendMenuW(menu, MF_STRING, 2, L"Abrir carpeta...\tCtrl + Shift + O");
    AppendMenuW(menu, MF_STRING, 17, L"Guardar imagen como...\tCtrl + S");
    AppendMenuW(menu, MF_STRING, 12, L"Mostrar en Explorador\tCtrl + E");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 3, L"Copiar imagen\tCtrl + C");
    AppendMenuW(menu, MF_STRING, 4, L"Copiar ruta\tCtrl + Shift + C");
    AppendMenuW(menu, MF_STRING, 19, L"Establecer como fondo de pantalla\tCtrl + W");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 5, L"Ajustar a ventana\tF");
    AppendMenuW(menu, MF_STRING, 6, L"Tamaño real (100%)\t1");
    AppendMenuW(menu, MF_STRING, 9, L"Acercar (+)\t+");
    AppendMenuW(menu, MF_STRING, 10, L"Alejar (-)\t-");
    AppendMenuW(menu, MF_STRING, 7, L"Rotar 90° horario\tR");
    AppendMenuW(menu, MF_STRING, 13, L"Volteo horizontal\tH");
    AppendMenuW(menu, MF_STRING, 14, L"Volteo vertical\tV");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 24, g_state.effectUltraClarity ? L"Desactivar Ultra-Claridad HDR\tD" : L"✨ Modo Ultra-Claridad (Detalles HDR)\tD");
    AppendMenuW(menu, MF_STRING, 20, g_state.effectGrayscale ? L"Desactivar escala de grises\tG" : L"Escala de grises (B/N)\tG");
    AppendMenuW(menu, MF_STRING, 21, g_state.effectInvert ? L"Desactivar invertir colores\tN" : L"Invertir colores (Negativo)\tN");
    AppendMenuW(menu, MF_STRING, 15, g_state.isSlideshowActive ? L"Detener presentación\tF5" : L"Iniciar presentación\tF5");
    AppendMenuW(menu, MF_STRING, 8, L"Pantalla completa\tF11");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 22, L"Ver metadatos / EXIF...\tE");
    AppendMenuW(menu, MF_STRING, 16, L"Eliminar a papelera\tSupr");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 11, L"Acerca de ARTPICST");

    const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, x, y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    switch (cmd) {
        case 1: OpenImageFileDialog(hwnd); break;
        case 2: OpenFolderDialog(hwnd); break;
        case 3: CopyImageToClipboard(); break;
        case 4: CopyPathToClipboard(); break;
        case 5: InvokeHud(HUD_FIT); break;
        case 6: ActualSize(); break;
        case 7: RotateImage(90); break;
        case 8: ToggleFullscreen(); break;
        case 9: {
            RECT client{};
            GetClientRect(hwnd, &client);
            ZoomAt(ZOOM_STEP, (client.right - client.left) / 2, (client.bottom - client.top) / 2);
            break;
        }
        case 10: {
            RECT client{};
            GetClientRect(hwnd, &client);
            ZoomAt(1.0f / ZOOM_STEP, (client.right - client.left) / 2, (client.bottom - client.top) / 2);
            break;
        }
        case 11: ShowAboutDialog(hwnd); break;
        case 12: OpenInExplorer(); break;
        case 13: FlipHorizontal(); break;
        case 14: FlipVertical(); break;
        case 15: ToggleSlideshow(); break;
        case 16: DeleteCurrentImage(); break;
        case 17: SaveImageDialog(hwnd); break;
        case 18: SetAsWallpaper(); break;
        case 19: ToggleGrayscale(); break;
        case 20: ToggleInvert(); break;
        case 21: ShowExifDialog(hwnd); break;
        case 22: ShowProgramInfoDialog(hwnd); break;
        case 23: ToggleUltraClarity(); break;
        default: break;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_state.hwnd = hwnd;
            DragAcceptFiles(hwnd, TRUE);
            EnableDarkTitleBar(hwnd);
            RECT client{};
            GetClientRect(hwnd, &client);
            CreateDoubleBuffer(client.right, client.bottom);
            StartPrefetchThread();
            ShowOSD();
            return 0;
        }
        case WM_NCCALCSIZE:
            if (wParam && g_state.isFullscreen) return 0;
            break;
        case WM_SIZE: {
            if (wParam == SIZE_MINIMIZED) return 0;
            const int width = LOWORD(lParam);
            const int height = HIWORD(lParam);
            CreateDoubleBuffer(width, height);
            if (g_state.imageData && width > 0 && height > 0) {
                if (g_state.fitMode) FitImageToWindow(width, height);
                else EnsureImageVisible();
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_DPICHANGED: {
            auto* suggested = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<LPMINMAXINFO>(lParam);
            info->ptMinTrackSize.x = 520;
            info->ptMinTrackSize.y = 360;
            return 0;
        }
        case WM_ERASEBKGND:
            return TRUE;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            if (!g_state.hdcMem) {
                RECT client{};
                GetClientRect(hwnd, &client);
                CreateDoubleBuffer(client.right, client.bottom);
            }
            if (g_state.hdcMem) {
                RenderImage();
                BitBlt(hdc, ps.rcPaint.left, ps.rcPaint.top,
                       ps.rcPaint.right - ps.rcPaint.left,
                       ps.rcPaint.bottom - ps.rcPaint.top,
                       g_state.hdcMem, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_KEYDOWN: {
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            switch (wParam) {
                case VK_LEFT:
                    PreviousImage();
                    break;
                case VK_RIGHT:
                    NextImage();
                    break;
                case VK_UP: {
                    RECT client{};
                    GetClientRect(hwnd, &client);
                    ZoomAt(ZOOM_STEP, (client.right - client.left) / 2, (client.bottom - client.top) / 2);
                    break;
                }
                case VK_DOWN: {
                    RECT client{};
                    GetClientRect(hwnd, &client);
                    ZoomAt(1.0f / ZOOM_STEP, (client.right - client.left) / 2, (client.bottom - client.top) / 2);
                    break;
                }
                case VK_BACK:
                    PreviousImage();
                    break;
                case VK_SPACE:
                    NextImage();
                    break;
                case VK_HOME:
                    JumpTo(0);
                    break;
                case VK_END:
                    if (ImageCount() > 0) JumpTo(ImageCount() - 1);
                    break;
                case VK_ESCAPE:
                    if (g_state.isFullscreen) ToggleFullscreen();
                    else if (g_state.isSlideshowActive) ToggleSlideshow();
                    else PostMessageW(hwnd, WM_CLOSE, 0, 0);
                    break;
                case VK_F11:
                    ToggleFullscreen();
                    break;
                case VK_F5:
                    ToggleSlideshow();
                    break;
                case VK_DELETE:
                    DeleteCurrentImage();
                    break;
                case VK_F1:
                    ShowProgramInfoDialog(hwnd);
                    break;
                case 'A':
                    if (ctrl && shift) {
                        ShowAboutDialog(hwnd);
                    }
                    break;
                case 'E':
                    if (ctrl) OpenInExplorer();
                    else ShowExifDialog(hwnd);
                    break;
                case 'S':
                    if (ctrl) SaveImageDialog(hwnd);
                    break;
                case 'W':
                    if (ctrl) SetAsWallpaper();
                    break;
                case 'U':
                    // Update functionality removed for lightweight implementation
                    break;
                case 'D':
                    ToggleUltraClarity();
                    break;
                case 'G':
                    ToggleGrayscale();
                    break;
                case 'N':
                    ToggleInvert();
                    break;
                case VK_OEM_PLUS:
                case VK_ADD: {
                    RECT client{};
                    GetClientRect(hwnd, &client);
                    ZoomAt(ZOOM_STEP, (client.right - client.left) / 2, (client.bottom - client.top) / 2);
                    break;
                }
                case VK_OEM_MINUS:
                case VK_SUBTRACT: {
                    RECT client{};
                    GetClientRect(hwnd, &client);
                    ZoomAt(1.0f / ZOOM_STEP, (client.right - client.left) / 2, (client.bottom - client.top) / 2);
                    break;
                }
                case 'I':
                    if (ctrl) ShowExifDialog(hwnd);
                    else {
                        g_state.osdPinned = !g_state.osdPinned;
                        ShowOSD(g_state.osdPinned ? L"Info fija" : L"Info auto");
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    break;
                case 'F':
                    InvokeHud(HUD_FIT);
                    break;
                case 'R':
                    RotateImage(shift ? -90 : 90);
                    break;
                case 'H':
                    FlipHorizontal();
                    break;
                case 'V':
                    FlipVertical();
                    break;
                case '0':
                case VK_NUMPAD0:
                case '1':
                    if (!ctrl) ActualSize();
                    break;
                case 'C':
                    if (ctrl && shift) CopyPathToClipboard();
                    else if (ctrl) CopyImageToClipboard();
                    break;
                case 'O':
                    if (ctrl && shift) OpenFolderDialog(hwnd);
                    else if (ctrl) OpenImageFileDialog(hwnd);
                    break;
                default:
                    break;
            }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            if (!g_state.imageData) return 0;
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);
            ZoomAt((delta > 0) ? ZOOM_STEP : (1.0f / ZOOM_STEP), pt.x, pt.y);
            return 0;
        }
        case WM_MBUTTONUP: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (!PtInRect(&g_state.dockRect, pt)) {
                if (g_state.fitMode) ActualSize();
                else InvokeHud(HUD_FIT);
            }
            return 0;
        }
        case WM_XBUTTONUP: {
            WORD btn = GET_XBUTTON_WPARAM(wParam);
            if (btn == XBUTTON1) PreviousImage();
            else if (btn == XBUTTON2) NextImage();
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (!PtInRect(&g_state.dockRect, pt)) {
                ToggleFullscreen();
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            const HudId hit = HitTestHud(x, y);
            if (hit != HUD_NONE) {
                InvokeHud(hit);
                return 0;
            }
            if (PtInRect(&g_state.dockRect, POINT{ x, y })) {
                return 0;
            }
            g_state.isDragging = true;
            g_state.dragStartX = x;
            g_state.dragStartY = y;
            g_state.dragStartOffsetX = g_state.offsetX;
            g_state.dragStartOffsetY = g_state.offsetY;
            SetCapture(hwnd);
            return 0;
        }
        case WM_LBUTTONUP:
            g_state.isDragging = false;
            ReleaseCapture();
            return 0;
        case WM_RBUTTONUP: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ClientToScreen(hwnd, &pt);
            ShowContextMenu(hwnd, pt.x, pt.y);
            return 0;
        }
        case WM_MOUSEMOVE: {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            
            // Dock proximity detection
            RECT client{};
            GetClientRect(hwnd, &client);
            const int distanceFromBottom = client.bottom - y;
            
            if (distanceFromBottom < DOCK_PROXIMITY_THRESHOLD) {
                if (!g_state.hudVisible) {
                    g_state.hudVisible = true;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                g_state.dockLastActivity = GetTickCount();
                KillTimer(hwnd, TIMER_DOCK_HIDE);
                SetTimer(hwnd, TIMER_DOCK_HIDE, DOCK_HIDE_MS, nullptr);
            }
            
            const HudId hot = HitTestHud(x, y);
            if (hot != g_state.hudHot) {
                g_state.hudHot = hot;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            if (g_state.isDragging) {
                g_state.offsetX = g_state.dragStartOffsetX + static_cast<float>(x - g_state.dragStartX);
                g_state.offsetY = g_state.dragStartOffsetY + static_cast<float>(y - g_state.dragStartY);
                g_state.fitMode = false;
                EnsureImageVisible();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (g_state.hudHot != HUD_NONE) {
                g_state.hudHot = HUD_NONE;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_TIMER:
            if (wParam == TIMER_OSD) {
                KillTimer(hwnd, TIMER_OSD);
                if (!g_state.osdPinned) {
                    g_state.statusMessage.clear();
                }
                g_state.showOSD = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (wParam == TIMER_SLIDESHOW) {
                NextImage();
            } else if (wParam == TIMER_DOCK_HIDE) {
                RECT client{};
                GetClientRect(hwnd, &client);
                POINT pt{};
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                const int distanceFromBottom = client.bottom - pt.y;
                
                if (distanceFromBottom >= DOCK_PROXIMITY_THRESHOLD) {
                    g_state.hudVisible = false;
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else {
                    // Cursor still near bottom, reset timer
                    g_state.dockLastActivity = GetTickCount();
                    SetTimer(hwnd, TIMER_DOCK_HIDE, DOCK_HIDE_MS, nullptr);
                }
            } else if (wParam == TIMER_GIF) {
                if (g_state.isGifAnimated && g_state.gifTotalFrames > 1) {
                    // Advance to next frame
                    g_state.gifCurrentFrame = (g_state.gifCurrentFrame + 1) % g_state.gifTotalFrames;
                    g_state.gifLastFrameTime = GetTickCount();
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        case WM_DROPFILES: {
            HDROP hDrop = reinterpret_cast<HDROP>(wParam);
            const UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
            if (fileCount > 0) {
                const UINT chars = DragQueryFileW(hDrop, 0, nullptr, 0);
                std::wstring filePath(chars, L'\0');
                if (DragQueryFileW(hDrop, 0, filePath.data(), chars + 1) > 0) {
                    if (!filePath.empty() && filePath.back() == L'\0') filePath.pop_back();
                    OpenPath(filePath);
                }
            }
            DragFinish(hDrop);
            return 0;
        }
        case WM_DESTROY:
            KillTimer(hwnd, TIMER_OSD);
            KillTimer(hwnd, TIMER_SLIDESHOW);
            KillTimer(hwnd, TIMER_DOCK_HIDE);
            KillTimer(hwnd, TIMER_GIF);
            StopPrefetchThread();
            FreeCurrentImage();
            FreeDoubleBuffer();
            g_state.hwnd = nullptr;
            g_state.statusMessage.clear();
            g_state.hudHot = HUD_NONE;
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool SetRegStringValue(HKEY root, const std::wstring& key, const std::wstring& name, const std::wstring& value) {
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(root, key.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS) return false;
    const DWORD type = REG_SZ;
    const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const bool ok = RegSetValueExW(hKey, name.empty() ? nullptr : name.c_str(), 0, type,
                                  reinterpret_cast<const BYTE*>(value.c_str()), bytes) == ERROR_SUCCESS;
    RegCloseKey(hKey);
    return ok;
}

bool RegisterFileAssociationForCurrentUser() {
    std::wstring exePath = GetExecutablePath();
    if (exePath.empty()) return false;
    const std::wstring fileType = L"ARTPICST.Image";
    const std::wstring command = L"\"" + exePath + L"\" \"%1\"";
    const std::vector<std::wstring> extensions = {
        L".png", L".jpg", L".jpeg", L".bmp", L".gif", L".tif", L".tiff",
        L".webp", L".ico", L".heic", L".heif", L".avif", L".jfif"
    };

    bool ok = true;
    ok = ok && SetRegStringValue(HKEY_CURRENT_USER, L"Software\\Classes\\" + fileType, L"", L"ARTPICST Image");
    ok = ok && SetRegStringValue(HKEY_CURRENT_USER, L"Software\\Classes\\" + fileType, L"Content Type", L"image/*");
    ok = ok && SetRegStringValue(HKEY_CURRENT_USER, L"Software\\Classes\\" + fileType, L"FriendlyTypeName", L"ARTPICST Image");
    ok = ok && SetRegStringValue(HKEY_CURRENT_USER, L"Software\\Classes\\" + fileType + L"\\DefaultIcon", L"", L"\"" + exePath + L"\",0");
    ok = ok && SetRegStringValue(HKEY_CURRENT_USER, L"Software\\Classes\\" + fileType + L"\\shell\\open\\command", L"", command);
    ok = ok && SetRegStringValue(HKEY_CURRENT_USER, L"Software\\Classes\\" + fileType + L"\\shell\\open", L"MuiVerb", L"Abrir");
    ok = ok && SetRegStringValue(HKEY_CURRENT_USER, L"Software\\Classes\\" + fileType + L"\\shell\\open", L"Icon", L"\"" + exePath + L"\",0");
    ok = ok && SetRegStringValue(HKEY_CURRENT_USER, L"Software\\Classes\\Applications\\artpicst.exe", L"FriendlyAppName", L"ARTPICST");
    ok = ok && SetRegStringValue(HKEY_CURRENT_USER, L"Software\\Classes\\Applications\\artpicst.exe\\shell\\open\\command", L"", command);
    ok = ok && SetRegStringValue(HKEY_CURRENT_USER, L"Software\\Classes\\Applications\\artpicst.exe\\shell\\open", L"MuiVerb", L"Abrir");
    for (const auto& ext : extensions) {
        ok = ok && SetRegStringValue(HKEY_CURRENT_USER, L"Software\\Classes\\" + ext, L"", fileType);
    }
    return ok;
}

bool UnregisterFileAssociationForCurrentUser() {
    std::vector<std::wstring> keys = {
        L"Software\\Classes\\ARTPICST.Image",
        L"Software\\Classes\\Applications\\artpicst.exe",
        L"Software\\Classes\\.png",
        L"Software\\Classes\\.jpg",
        L"Software\\Classes\\.jpeg",
        L"Software\\Classes\\.bmp",
        L"Software\\Classes\\.gif",
        L"Software\\Classes\\.tif",
        L"Software\\Classes\\.tiff",
        L"Software\\Classes\\.webp",
        L"Software\\Classes\\.ico",
        L"Software\\Classes\\.heic",
        L"Software\\Classes\\.heif",
        L"Software\\Classes\\.avif",
        L"Software\\Classes\\.jfif"
    };
    bool ok = true;
    for (const auto& key : keys) {
        ok = ok && (RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str()) == ERROR_SUCCESS || RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str()) == ERROR_FILE_NOT_FOUND);
    }
    return ok;
}

void ParseStartupOptions(std::wstring& outFolder, WindowMode& outMode) {
    outFolder.clear();
    outMode = WindowMode::Normal;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return;

    std::wstring firstPath;
    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg == L"--register") {
            RegisterFileAssociationForCurrentUser();
            PostQuitMessage(0);
            LocalFree(argv);
            return;
        }
        if (arg == L"--unregister") {
            UnregisterFileAssociationForCurrentUser();
            PostQuitMessage(0);
            LocalFree(argv);
            return;
        }
        if (arg == L"--fullscreen" || arg == L"-f") {
            outMode = WindowMode::Fullscreen;
            continue;
        }
        if (arg == L"--normal" || arg == L"-n") {
            outMode = WindowMode::Normal;
            continue;
        }
        if (arg == L"--maximized" || arg == L"-m") {
            outMode = WindowMode::Maximized;
            continue;
        }
        if (arg.empty() || arg[0] == L'-') continue;
        if (firstPath.empty()) firstPath = arg;
    }
    LocalFree(argv);

    if (firstPath.empty()) return;
    if (firstPath.size() >= 2 && firstPath.front() == L'"' && firstPath.back() == L'"') {
        firstPath = firstPath.substr(1, firstPath.size() - 2);
    }
    firstPath = NormalizePath(firstPath);
    DWORD attrs = GetFileAttributesW(firstPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        g_state.startupFilePath = firstPath;
        return;
    }
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
        outFolder = firstPath;
    } else {
        g_state.startupFilePath = firstPath;
        size_t pos = firstPath.find_last_of(L"\\/");
        if (pos != std::wstring::npos) outFolder = firstPath.substr(0, pos);
    }
}

static void EnableDpiAwareness() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        using SetCtxFn = BOOL (WINAPI*)(HANDLE);
        auto setCtx = reinterpret_cast<SetCtxFn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setCtx && setCtx(reinterpret_cast<HANDLE>(-4))) return;
    }
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    if (shcore) {
        using SetAwarenessFn = HRESULT (WINAPI*)(int);
        auto setAwareness = reinterpret_cast<SetAwarenessFn>(GetProcAddress(shcore, "SetProcessDpiAwareness"));
        if (setAwareness && SUCCEEDED(setAwareness(2))) {
            FreeLibrary(shcore);
            return;
        }
        FreeLibrary(shcore);
    }
    SetProcessDPIAware();
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    EnableDpiAwareness();
    g_state.hInstance = hInstance;

    HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    g_state.comHr = comHr;
    g_state.comInitialized = SUCCEEDED(comHr);
    LogMessage(L"Iniciando ARTPICST");

    if (!InitGDIPlus()) {
        ShowThemedMessageBox(nullptr, L"ARTPICST", L"Error al inicializar GDI+.", MB_OK, MB_ICONERROR);
        if (SUCCEEDED(g_state.comHr)) CoUninitialize();
        return 1;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    g_state.classBrush = CreateSolidBrush(BG_COLOR);
    wc.hbrBackground = g_state.classBrush;
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    if (!wc.hIcon) {
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    wc.hIconSm = wc.hIcon;

    if (!RegisterClassExW(&wc)) {
        ShowThemedMessageBox(nullptr, L"ARTPICST", L"Error al registrar la ventana.", MB_OK, MB_ICONERROR);
        if (g_state.classBrush) {
            DeleteObject(g_state.classBrush);
            g_state.classBrush = nullptr;
        }
        CleanupGDIPlus();
        if (SUCCEEDED(g_state.comHr)) CoUninitialize();
        return 1;
    }

    std::wstring folderPath;
    WindowMode startMode = WindowMode::Normal;
    ParseStartupOptions(folderPath, startMode);
    g_state.windowMode = startMode;
    g_state.isFullscreen = (startMode == WindowMode::Fullscreen);
    if (!folderPath.empty()) ScanFolderForImages(folderPath);

    HWND hwnd = CreateWindowExW(
        WS_EX_ACCEPTFILES | WS_EX_APPWINDOW,
        CLASS_NAME,
        L"ARTPICST",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        CW_USEDEFAULT, CW_USEDEFAULT, 1200, 800,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) {
        ShowThemedMessageBox(nullptr, L"ARTPICST", L"Error al crear la ventana.", MB_OK, MB_ICONERROR);
        UnregisterClassW(CLASS_NAME, hInstance);
        if (g_state.classBrush) {
            DeleteObject(g_state.classBrush);
            g_state.classBrush = nullptr;
        }
        CleanupGDIPlus();
        if (SUCCEEDED(g_state.comHr)) CoUninitialize();
        return 1;
    }

    EnableDarkTitleBar(hwnd);
    LoadInitialImageSafely();
    ApplyWindowMode(hwnd, startMode);
    ShowWindow(hwnd, g_state.isFullscreen ? SW_SHOW : (nCmdShow > 0 ? nCmdShow : SW_SHOWDEFAULT));
    InvalidateRect(hwnd, nullptr, FALSE);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CleanupGDIPlus();
    UnregisterClassW(CLASS_NAME, hInstance);
    if (g_state.classBrush) {
        DeleteObject(g_state.classBrush);
        g_state.classBrush = nullptr;
    }
    if (SUCCEEDED(g_state.comHr)) CoUninitialize();
    return static_cast<int>(msg.wParam);
}
