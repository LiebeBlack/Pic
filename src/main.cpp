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
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

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
#include <d2d1.h>
#include <dwrite.h>
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
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

using namespace Gdiplus;

// Configuración de aplicación y constantes visuales
const wchar_t CLASS_NAME[] = L"ARTPICSTWindow";
const wchar_t APP_NAME_TEXT[] = L"ARTPICST";
const wchar_t APP_VERSION_TEXT[] = L"1.2.0";

// Sistema de temas inteligente
enum class ThemeMode {
    Auto,           // Detectar automáticamente del sistema
    Dark,           // Forzar tema oscuro
    Light           // Forzar tema claro
};

// Sistema de tamaño adaptativo
enum class UISize {
    Small,          // Para pantallas pequeñas
    Medium,         // Tamaño estándar
    Large           // Para pantallas grandes
};

// Tema oscuro suave y descansado (estilo Win10/7 con toques Win11, sin fatiga visual)
const COLORREF BG_COLOR_DARK = RGB(26, 26, 30);
const COLORREF CHECKER_A_DARK = RGB(32, 32, 38);
const COLORREF CHECKER_B_DARK = RGB(40, 40, 48);

// Tema claro limpio estilo Windows
const COLORREF BG_COLOR_LIGHT = RGB(242, 244, 247);
const COLORREF CHECKER_A_LIGHT = RGB(232, 234, 238);
const COLORREF CHECKER_B_LIGHT = RGB(220, 222, 226);

// Colores actuales (se establecen dinámicamente)
COLORREF BG_COLOR = BG_COLOR_DARK;
COLORREF CHECKER_A = CHECKER_A_DARK;
COLORREF CHECKER_B = CHECKER_B_DARK;

// Dock y botones ultraligeros estilo Windows 10/7 con bordes suaves de Windows 11
const Color GLASS_DOCK_BG_DARK(255, 34, 35, 42);
const Color GLASS_DOCK_BORDER_DARK(255, 58, 60, 72);
const Color GLASS_DOCK_SHADOW_DARK(30, 0, 0, 0);
const Color GLASS_BTN_NORMAL_DARK(255, 45, 46, 56);
const Color GLASS_BTN_BORDER_NORMAL_DARK(255, 68, 70, 84);
const Color GLASS_BTN_HOT_DARK(255, 0, 120, 215);
const Color GLASS_BTN_BORDER_HOT_DARK(255, 96, 180, 242);
const Color GLASS_BTN_ACTIVE_DARK(255, 0, 99, 177);

const Color GLASS_DOCK_BG_LIGHT(255, 245, 247, 250);
const Color GLASS_DOCK_BORDER_LIGHT(255, 208, 213, 220);
const Color GLASS_DOCK_SHADOW_LIGHT(20, 0, 0, 0);
const Color GLASS_BTN_NORMAL_LIGHT(255, 255, 255);
const Color GLASS_BTN_BORDER_NORMAL_LIGHT(255, 212, 216, 224);
const Color GLASS_BTN_HOT_LIGHT(255, 0, 120, 215);
const Color GLASS_BTN_BORDER_HOT_LIGHT(255, 0, 99, 177);
const Color GLASS_BTN_ACTIVE_LIGHT(255, 0, 99, 177);

// Colores actuales de interfaz (se establecen dinámicamente)
Color GLASS_DOCK_BG = GLASS_DOCK_BG_DARK;
Color GLASS_DOCK_BORDER = GLASS_DOCK_BORDER_DARK;
Color GLASS_DOCK_SHADOW = GLASS_DOCK_SHADOW_DARK;
Color GLASS_BTN_NORMAL = GLASS_BTN_NORMAL_DARK;
Color GLASS_BTN_BORDER_NORMAL = GLASS_BTN_BORDER_NORMAL_DARK;
Color GLASS_BTN_HOT = GLASS_BTN_HOT_DARK;
Color GLASS_BTN_BORDER_HOT = GLASS_BTN_BORDER_HOT_DARK;
Color GLASS_BTN_ACTIVE = GLASS_BTN_ACTIVE_DARK;

// Configuración de zoom y renderizado ultraligero (Consumo mínimo de RAM y CPU)
const float MIN_ZOOM = 0.01f;
const float MAX_ZOOM = 200.0f;
const float ZOOM_STEP = 1.25f;
const size_t CACHE_SIZE = 4;                           // Caché compacta (2 previas + 2 siguientes) para consumo ultra-ligero
const size_t MAX_CACHE_BYTES = 64ull * 1024ull * 1024ull; // 64 MB límite de caché: evita picos de RAM (~300 MB) con fotos grandes
const int MAX_DIMENSION = 3840;
const LONGLONG MAX_FILE_BYTES = 300LL * 1024LL * 1024LL;
const int MAX_GIF_FRAMES = 120;
const size_t MAX_GIF_BYTES = 64ull * 1024ull * 1024ull;

// Configuración de renderizado ultraligero - efectos desactivados
const bool ENABLE_ULTRA_QUALITY_RENDERING = false;
const bool ENABLE_ADAPTIVE_SHARPNESS = false;
const bool ENABLE_AUTO_CONTRAST = false;
const bool ENABLE_GAMMA_CORRECTION = false;
const bool ENABLE_BLUR_EFFECTS = false;                 // Desactivar desenfoques pesados
const bool ENABLE_TRANSPARENCY_EFFECTS = false;         // Desactivar transparencias pesadas

// Sistema de tamaño UI adaptativo
const int UI_SCALE_SMALL = 80;      // 80% del tamaño normal
const int UI_SCALE_MEDIUM = 100;    // 100% del tamaño normal
const int UI_SCALE_LARGE = 120;    // 120% del tamaño normal

// Estado de tema y tamaño
ThemeMode g_currentTheme = ThemeMode::Auto;
UISize g_currentUISize = UISize::Medium;
int g_uiScale = UI_SCALE_MEDIUM;

// Configuración de temporizadores
const UINT OSD_MS = 3500;
const UINT_PTR TIMER_OSD = 1;
const UINT_PTR TIMER_SLIDESHOW = 2;
const UINT SLIDESHOW_INTERVAL_MS = 3500;
const UINT_PTR TIMER_DOCK_HIDE = 3;
const UINT DOCK_HIDE_MS = 2000;
const int DOCK_PROXIMITY_THRESHOLD = 80;
const UINT_PTR TIMER_GIF = 4;
const UINT_PTR TIMER_ZOOM = 5;
const DWORD ZOOM_ANIM_MS = 110;   // Duración de la animación de zoom suave

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

static void FreePixels(unsigned char*& p) {
    if (p) {
        free(p);
        p = nullptr;
    }
}

struct GifAnimation {
    std::vector<unsigned char*> frames;
    std::vector<int> delaysMs;
    int current = 0;

    void reset() {
        for (unsigned char* f : frames) {
            if (f) free(f);
        }
        frames.clear();
        delaysMs.clear();
        current = 0;
    }

    GifAnimation() = default;
    ~GifAnimation() { reset(); }
    GifAnimation(const GifAnimation&) = delete;
    GifAnimation& operator=(const GifAnimation&) = delete;
    GifAnimation(GifAnimation&& other) noexcept
        : frames(std::move(other.frames)), delaysMs(std::move(other.delaysMs)), current(other.current) {
        other.frames.clear();
        other.delaysMs.clear();
        other.current = 0;
    }
    GifAnimation& operator=(GifAnimation&& other) noexcept {
        if (this != &other) {
            reset();
            frames = std::move(other.frames);
            delaysMs = std::move(other.delaysMs);
            current = other.current;
            other.frames.clear();
            other.delaysMs.clear();
            other.current = 0;
        }
        return *this;
    }

    bool animated() const { return frames.size() > 1 && frames.size() == delaysMs.size(); }
    int frameCount() const { return static_cast<int>(frames.size()); }

    unsigned char* currentFramePixels() const {
        if (animated() && current >= 0 && static_cast<size_t>(current) < frames.size()) {
            return frames[static_cast<size_t>(current)];
        }
        return nullptr;
    }

    int delayAt(int index) const {
        if (index < 0 || index >= static_cast<int>(delaysMs.size())) return 100;
        int d = delaysMs[static_cast<size_t>(index)];
        if (d < 20) d = 100;
        if (d > 10000) d = 10000;
        return d;
    }
};

struct CachedImage {
    unsigned char* data = nullptr;
    int width = 0, height = 0, channels = 0, rotation = 0;
    bool flipH = false, flipV = false, hasAlpha = false;
    std::wstring filepath, decoder;

    CachedImage() = default;
    ~CachedImage() { FreePixels(data); }
    CachedImage(const CachedImage&) = delete;
    CachedImage& operator=(const CachedImage&) = delete;
    CachedImage(CachedImage&& other) noexcept
        : data(other.data), width(other.width), height(other.height),
          channels(other.channels), rotation(other.rotation),
          flipH(other.flipH), flipV(other.flipV), hasAlpha(other.hasAlpha),
          filepath(std::move(other.filepath)), decoder(std::move(other.decoder)) {
        other.data = nullptr;
        other.width = other.height = other.channels = other.rotation = 0;
        other.flipH = other.flipV = other.hasAlpha = false;
    }
    CachedImage& operator=(CachedImage&& other) noexcept {
        if (this != &other) {
            FreePixels(data);
            data = other.data;
            width = other.width; height = other.height; channels = other.channels;
            rotation = other.rotation; flipH = other.flipH; flipV = other.flipV;
            hasAlpha = other.hasAlpha;
            filepath = std::move(other.filepath); decoder = std::move(other.decoder);
            other.data = nullptr;
            other.width = other.height = other.channels = other.rotation = 0;
            other.flipH = other.flipV = other.hasAlpha = false;
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

    // Sincronización de fotogramas GIF (evita deriva de tiempo)
    DWORD gifLastTick = 0;

    // Animación de zoom suave (interpolada por temporizador)
    bool zoomAnimActive = false;
    float zoomAnimStart = 0.0f, zoomAnimTarget = 0.0f;
    float zoomAnimImageX = 0.0f, zoomAnimImageY = 0.0f;
    float zoomAnimStartOffsetX = 0.0f, zoomAnimStartOffsetY = 0.0f;
    DWORD zoomAnimStartTime = 0;

    // Sistema de caché
    std::unordered_map<std::wstring, std::list<CachedImage>::iterator> cacheIndex;
    std::list<CachedImage> imageCache;
    std::mutex cacheMutex;
    size_t cacheMemoryBytes = 0;

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
    
    // Sistema de tema inteligente
    bool darkModeDetected = false;
    bool themeInitialized = false;

    bool isFullscreen = false;
    WindowMode windowMode = WindowMode::Normal;
    WINDOWPLACEMENT windowedPlacement{};
    LONG windowedStyle = 0;
    LONG windowedExStyle = 0;

    RECT dockRect{};
    HudItem hud[12]{};
    int hudCount = 0;
    HudId hudHot = HUD_NONE;
    bool dockAutoHide = true;
    DWORD dockLastActivity = 0;

    GifAnimation gif;

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
        FreePixels(imageData);
        gif.reset();
    }
};

AppState g_state;

// Se activa cuando un argumento de línea de comandos (--register/--unregister)
// ya completó su trabajo y la aplicación debe salir sin crear la ventana.
bool g_exitAfterCommandLine = false;

// Recursos GDI+ reutilizables: se crean UNA sola vez tras iniciar GDI+
// y se destruyen antes de GdiplusShutdown. Evita crear fuentes y formatos
// en cada fotograma (muy costoso) y no deja fugas de memoria.
struct UiResources {
    std::unique_ptr<FontFamily> segoeFamily;
    std::unique_ptr<Font> hudBtnFont;      // Botones del dock
    std::unique_ptr<Font> osdFont;         // Mensajes OSD
    std::unique_ptr<Font> emptyTitleFont;  // Pantalla vacía
    std::unique_ptr<Font> emptyHintFont;   // Pantalla vacía
    std::unique_ptr<StringFormat> centerFormat;

    void Init() {
        if (segoeFamily) return;
        segoeFamily = std::make_unique<FontFamily>(L"Segoe UI");
        hudBtnFont = std::make_unique<Font>(segoeFamily.get(), 9.5f, FontStyleBold, UnitPoint);
        osdFont = std::make_unique<Font>(segoeFamily.get(), 10.5f, FontStyleBold, UnitPoint);
        emptyTitleFont = std::make_unique<Font>(segoeFamily.get(), 28.0f, FontStyleBold, UnitPoint);
        emptyHintFont = std::make_unique<Font>(segoeFamily.get(), 11.5f, FontStyleRegular, UnitPoint);
        centerFormat = std::make_unique<StringFormat>();
        centerFormat->SetAlignment(StringAlignmentCenter);
        centerFormat->SetLineAlignment(StringAlignmentCenter);
    }

    void Shutdown() {
        centerFormat.reset();
        emptyHintFont.reset();
        emptyTitleFont.reset();
        osdFont.reset();
        hudBtnFont.reset();
        segoeFamily.reset();
    }
};

UiResources g_ui;

std::wstring GetFileName(const std::wstring& filepath);
std::wstring GetFileSizeString(const std::wstring& filepath);
std::wstring GetExtensionLower(const std::wstring& filename);
bool PathsEqualCaseInsensitive(const std::wstring& a, const std::wstring& b);
void FreeCurrentImage();
void ScanFolderForImages(const std::wstring& folderPath);
bool LoadImageFromPath(const std::wstring& filepath);
bool LoadImageByIndex(size_t index);
void RequestPrefetch(size_t targetIndex);
void StartPrefetchThread();
void StopPrefetchThread();

// Funciones de sistema inteligente
bool DetectSystemDarkMode();
void ApplyTheme(ThemeMode theme);
void InitializeIntelligentTheme();
void DetectOptimalUISize();
void ApplyUISize(UISize size);
void InitializeIntelligentUI();

// Funciones de texto inteligente
struct TextSizeInfo {
    float optimalFontSize;
    int optimalWidth;
    int optimalHeight;
    int lineCount;
    bool needsScrolling;
};

TextSizeInfo CalculateOptimalTextSize(const wchar_t* text, int maxWidth, int maxHeight, const wchar_t* fontName = L"Segoe UI");
void CalculateDialogSize(const wchar_t* title, const wchar_t* message, UINT buttons, int& outWidth, int& outHeight);
float GetAdaptiveFontSize(const wchar_t* text, int availableWidth, int availableHeight, const wchar_t* fontName = L"Segoe UI");
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
void RenderImage(const RECT* clipRect = nullptr);
void FreeCheckerTile();
void LoadInitialImageSafely();
bool CopyPathToClipboard();
bool CopyImageToClipboard();
void ShowOSD(const std::wstring& status = L"");
void NextImage();
void PreviousImage();
void OpenPath(const std::wstring& path);
void UpdateWindowTitle();
void EnableDarkTitleBar(HWND hwnd, bool dark = true);
void StopGifTimer();
void StartGifTimer();
int ShowThemedMessageBox(HWND parent, const wchar_t* title, const wchar_t* message, UINT buttons, UINT icon);
bool OpenImageFileDialog(HWND hwnd);
bool OpenFolderDialog(HWND hwnd);
bool SaveImageDialog(HWND hwnd);
void SetAsWallpaper();
void ShowExifDialog(HWND hwnd);
void ShowProgramInfoDialog(HWND hwnd);
void ShowAboutDialog(HWND hwnd);
void ToggleTheme();
void LayoutHud(const RECT& client);
HudId HitTestHud(int x, int y);
void InvokeHud(HudId id);
std::wstring WideToLower(std::wstring value);
std::string WideToUtf8(const std::wstring& value);
void ApplyWindowMode(HWND hwnd, WindowMode mode);
std::wstring NormalizePath(const std::wstring& path);
void ZoomAt(float factor, int pivotX, int pivotY);
void CancelZoomAnimation();
void DeleteCurrentImage();
void OpenInExplorer();

// Renderizador GPU (Direct2D): declaraciones usadas antes de su definición
bool GpuRenderFrame();
void GpuReleaseAll();
void GpuShutdown();

static bool SafePixelBytes(int width, int height, size_t& outBytes) {
    if (width <= 0 || height <= 0) return false;
    if (width > MAX_DIMENSION || height > MAX_DIMENSION) return false;
    const uint64_t bytes = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4ull;
    if (bytes > static_cast<uint64_t>(SIZE_MAX)) return false;
    outBytes = static_cast<size_t>(bytes);
    return true;
}

// Libera páginas de memoria física no utilizadas devolviéndolas al sistema operativo
static void TrimProcessMemory() {
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
}

static void DownscaleImageIfTooLarge(unsigned char*& pixels, int& width, int& height) {
    if (!pixels || width <= 0 || height <= 0) return;
    if (width <= MAX_DIMENSION && height <= MAX_DIMENSION) return;

    const float scale = std::min(static_cast<float>(MAX_DIMENSION) / width, static_cast<float>(MAX_DIMENSION) / height);
    const int newWidth = std::max(1, static_cast<int>(width * scale + 0.5f));
    const int newHeight = std::max(1, static_cast<int>(height * scale + 0.5f));

    unsigned char* resized = stbir_resize_uint8_srgb(
        pixels, width, height, 0,
        nullptr, newWidth, newHeight, 0,
        STBIR_BGRA);
    if (!resized) return;

    FreePixels(pixels);
    pixels = resized;
    width = newWidth;
    height = newHeight;
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

// Sistema de diálogos personalizados con tema.
// El estado es PROPIO DE CADA VENTANA (guardado en GWLP_USERDATA): así dos
// diálogos anidados (p. ej. un error mostrado encima de otro diálogo) no se
// sobrescriben ni corrompen entre sí.
struct ThemedDialogState {
    std::wstring title;
    std::wstring message;
    UINT buttons = MB_OK;
    UINT icon = MB_ICONINFORMATION;
    int result = IDOK;
};

ThemedDialogState* DialogStateFrom(HWND hwnd) {
    return reinterpret_cast<ThemedDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

// Declaración anticipada: AddRoundedRect se define más abajo pero se usa en ThemedDialogProc
void AddRoundedRect(GraphicsPath& path, const RectF& rect, float radius);

void EnableDarkTitleBar(HWND hwnd, bool dark) {
    if (!hwnd) return;
    BOOL useDark = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, 20, &useDark, sizeof(useDark));
    DwmSetWindowAttribute(hwnd, 19, &useDark, sizeof(useDark));

    COLORREF captionColor = dark ? RGB(32, 32, 32) : RGB(243, 243, 243);
    COLORREF textColor = dark ? RGB(250, 250, 250) : RGB(32, 32, 32);
    DwmSetWindowAttribute(hwnd, 35, &captionColor, sizeof(captionColor));
    DwmSetWindowAttribute(hwnd, 36, &textColor, sizeof(textColor));

    DWORD backdropType = 1; // DWMSBT_NONE — sin Mica/Acrílico (ligero)
    DwmSetWindowAttribute(hwnd, 38, &backdropType, sizeof(backdropType));
}

void StopGifTimer() {
    if (g_state.hwnd) KillTimer(g_state.hwnd, TIMER_GIF);
}

void StartGifTimer() {
    StopGifTimer();
    if (!g_state.hwnd || !g_state.gif.animated()) return;
    g_state.gifLastTick = GetTickCount();
    SetTimer(g_state.hwnd, TIMER_GIF, static_cast<UINT>(g_state.gif.delayAt(g_state.gif.current)), nullptr);
}

LRESULT CALLBACK ThemedDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_NCCREATE: {
            // El estado específico de esta ventana viaja como lpCreateParams
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
        case WM_CREATE: {
            // La barra de título debe seguir el tema activo, no forzar oscuro
            EnableDarkTitleBar(hwnd, g_state.darkModeDetected);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            ThemedDialogState* st = DialogStateFrom(hwnd);
            if (!st) {
                EndPaint(hwnd, &ps);
                return 0;
            }
            
            Graphics graphics(hdc);
            graphics.SetCompositingQuality(CompositingQualityHighSpeed);
            graphics.SetSmoothingMode(SmoothingModeAntiAlias);
            graphics.SetPixelOffsetMode(PixelOffsetModeHalf);
            graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
            
            RECT client;
            GetClientRect(hwnd, &client);
            
            const bool isDark = g_state.darkModeDetected;
            Color bgCol = isDark ? Color(255, 24, 25, 28) : Color(255, 246, 248, 250);
            Color borderCol = isDark ? Color(255, 48, 52, 60) : Color(255, 218, 222, 228);
            Color titleCol = isDark ? Color(255, 245, 248, 252) : Color(255, 26, 28, 32);
            Color textCol = isDark ? Color(255, 215, 220, 228) : Color(255, 48, 52, 60);

            SolidBrush bgBrush(bgCol);
            graphics.FillRectangle(&bgBrush, 0, 0, client.right, client.bottom);
            
            Pen borderPen(borderCol, 1.0f);
            graphics.DrawRectangle(&borderPen, 0, 0, client.right - 1, client.bottom - 1);
            
            // Icon - Elegante y estilizado con símbolo vectorial centrado
            const int iconSize = 34 * g_uiScale / 100;
            const int iconX = 26 * g_uiScale / 100;
            const int iconY = 22 * g_uiScale / 100;
            RectF iconRect(static_cast<float>(iconX), static_cast<float>(iconY), static_cast<float>(iconSize), static_cast<float>(iconSize));
            
            Color iconBg;
            const wchar_t* iconSymbol = L"i";
            Color iconSymbolCol(255, 255, 255, 255);
            
            if (st->icon == MB_ICONERROR) {
                iconBg = Color(255, 220, 48, 54);
                iconSymbol = L"✕";
            } else if (st->icon == MB_ICONWARNING) {
                iconBg = Color(255, 225, 145, 10);
                iconSymbol = L"!";
            } else if (st->icon == MB_ICONQUESTION) {
                iconBg = Color(255, 0, 120, 215);
                iconSymbol = L"?";
            } else {
                iconBg = Color(255, 0, 120, 215);
                iconSymbol = L"i";
            }
            
            SolidBrush iconBrush(iconBg);
            graphics.FillEllipse(&iconBrush, iconRect);

            Gdiplus::FontFamily iconFamily(L"Segoe UI");
            Gdiplus::Font iconSymbolFont(&iconFamily, 12.0f, FontStyleBold, UnitPoint);
            StringFormat iconFormat;
            iconFormat.SetAlignment(StringAlignmentCenter);
            iconFormat.SetLineAlignment(StringAlignmentCenter);
            SolidBrush symbolBrush(iconSymbolCol);
            graphics.DrawString(iconSymbol, -1, &iconSymbolFont, iconRect, &iconFormat, &symbolBrush);

            // Title
            if (!st->title.empty()) {
                float titleFontSize = GetAdaptiveFontSize(st->title.c_str(), client.right - 120, 50, L"Segoe UI");
                Gdiplus::FontFamily titleFamily(L"Segoe UI");
                Gdiplus::Font titleFont(&titleFamily, titleFontSize, FontStyleBold, UnitPoint);
                StringFormat titleFormat;
                titleFormat.SetAlignment(StringAlignmentNear);
                titleFormat.SetLineAlignment(StringAlignmentCenter);
                titleFormat.SetTrimming(StringTrimmingEllipsisCharacter);
                
                SolidBrush titleBrush(titleCol);
                const float titleX = static_cast<float>(iconX + iconSize + 14);
                RectF titleRect(titleX, static_cast<float>(iconY - 4), static_cast<float>(client.right - titleX - 20), static_cast<float>(iconSize + 8));
                graphics.DrawString(st->title.c_str(), -1, &titleFont, titleRect, &titleFormat, &titleBrush);
            }

            // Separador horizontal sutil
            Pen sepPen(borderCol, 1.0f);
            const float sepY = static_cast<float>(iconY + iconSize + 14);
            graphics.DrawLine(&sepPen, 24.0f, sepY, static_cast<float>(client.right - 24), sepY);

            // Message - Presentación legible
            if (!st->message.empty()) {
                const bool hasNewlines = (st->message.find(L'\n') != std::wstring::npos);
                float messageFontSize = GetAdaptiveFontSize(st->message.c_str(), client.right - 60, client.bottom - 160, L"Segoe UI");
                Gdiplus::FontFamily messageFamily(L"Segoe UI");
                Gdiplus::Font messageFont(&messageFamily, messageFontSize, FontStyleRegular, UnitPoint);
                StringFormat messageFormat;
                messageFormat.SetAlignment(hasNewlines ? StringAlignmentNear : StringAlignmentCenter);
                messageFormat.SetLineAlignment(hasNewlines ? StringAlignmentNear : StringAlignmentCenter);
                messageFormat.SetTrimming(StringTrimmingEllipsisWord);
                
                SolidBrush messageBrush(textCol);
                RectF messageRect(28.0f, sepY + 14.0f, static_cast<float>(client.right - 56), static_cast<float>(client.bottom - sepY - 74.0f));
                graphics.DrawString(st->message.c_str(), -1, &messageFont, messageRect, &messageFormat, &messageBrush);
            }
            
            // Buttons - Diseño refinado
            const int buttonWidth = 110;
            const int buttonHeight = 32;
            const int buttonY = client.bottom - 46;
            
            Color btnPrimaryBg(255, 0, 120, 215);
            Color btnPrimaryBorder(255, 96, 180, 242);
            Color btnSecondaryBg = isDark ? Color(255, 48, 50, 60) : Color(255, 230, 234, 240);
            Color btnSecondaryBorder = isDark ? Color(255, 72, 76, 88) : Color(255, 190, 196, 206);
            Color btnSecondaryText = isDark ? Color(255, 240, 244, 250) : Color(255, 30, 32, 38);

            Gdiplus::FontFamily btnFamily(L"Segoe UI");
            Gdiplus::Font buttonFont(&btnFamily, 10.5f, FontStyleBold, UnitPoint);
            StringFormat buttonFormat;
            buttonFormat.SetAlignment(StringAlignmentCenter);
            buttonFormat.SetLineAlignment(StringAlignmentCenter);

            if (st->buttons == MB_YESNO) {
                const int yesX = client.right / 2 - buttonWidth - 10;
                const int noX = client.right / 2 + 10;
                
                // Yes button (Primary)
                GraphicsPath yesPath;
                RectF yesRect(static_cast<float>(yesX), static_cast<float>(buttonY), static_cast<float>(buttonWidth), static_cast<float>(buttonHeight));
                AddRoundedRect(yesPath, yesRect, 6.0f);
                SolidBrush yesBrush(btnPrimaryBg);
                Pen yesBorder(btnPrimaryBorder, 1.0f);
                graphics.FillPath(&yesBrush, &yesPath);
                graphics.DrawPath(&yesBorder, &yesPath);
                
                // No button (Secondary)
                GraphicsPath noPath;
                RectF noRect(static_cast<float>(noX), static_cast<float>(buttonY), static_cast<float>(buttonWidth), static_cast<float>(buttonHeight));
                AddRoundedRect(noPath, noRect, 6.0f);
                SolidBrush noBrush(btnSecondaryBg);
                Pen noBorder(btnSecondaryBorder, 1.0f);
                graphics.FillPath(&noBrush, &noPath);
                graphics.DrawPath(&noBorder, &noPath);
                
                SolidBrush yesTextBrush(Color(255, 255, 255, 255));
                graphics.DrawString(L"Sí", -1, &buttonFont, yesRect, &buttonFormat, &yesTextBrush);
                
                SolidBrush noTextBrush(btnSecondaryText);
                graphics.DrawString(L"No", -1, &buttonFont, noRect, &buttonFormat, &noTextBrush);
            } else {
                const int okX = (client.right - buttonWidth) / 2;
                
                GraphicsPath okPath;
                RectF okRect(static_cast<float>(okX), static_cast<float>(buttonY), static_cast<float>(buttonWidth), static_cast<float>(buttonHeight));
                AddRoundedRect(okPath, okRect, 6.0f);
                SolidBrush okBrush(btnPrimaryBg);
                Pen okBorder(btnPrimaryBorder, 1.0f);
                graphics.FillPath(&okBrush, &okPath);
                graphics.DrawPath(&okBorder, &okPath);
                
                SolidBrush okTextBrush(Color(255, 255, 255, 255));
                graphics.DrawString(L"Aceptar", -1, &buttonFont, okRect, &buttonFormat, &okTextBrush);
            }
            
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return TRUE;
        case WM_DESTROY:
            return 0;
        case WM_LBUTTONDOWN: {
            ThemedDialogState* st = DialogStateFrom(hwnd);
            if (!st) return DefWindowProcW(hwnd, msg, wParam, lParam);
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            
            RECT client;
            GetClientRect(hwnd, &client);
            
            int buttonWidth = 110;
            int buttonHeight = 32;
            int buttonY = client.bottom - 46;
            
            // Check button clicks
            if (st->buttons == MB_YESNO) {
                int yesX = client.right / 2 - buttonWidth - 10;
                int noX = client.right / 2 + 10;
                
                if (x >= yesX && x <= yesX + buttonWidth && y >= buttonY && y <= buttonY + buttonHeight) {
                    st->result = IDYES;
                    DestroyWindow(hwnd);
                } else if (x >= noX && x <= noX + buttonWidth && y >= buttonY && y <= buttonY + buttonHeight) {
                    st->result = IDNO;
                    DestroyWindow(hwnd);
                }
            } else {
                int okX = (client.right - buttonWidth) / 2;
                if (x >= okX && x <= okX + buttonWidth && y >= buttonY && y <= buttonY + buttonHeight) {
                    st->result = IDOK;
                    DestroyWindow(hwnd);
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
    
    // Estado PROPIO de esta llamada: viaja como lpCreateParams a la ventana y se
    // guarda en GWLP_USERDATA. Vive mientras dura este bucle modal, así que los
    // diálogos anidados no comparten estado.
    ThemedDialogState state;
    state.title = title ? title : L"";
    state.message = message ? message : L"";
    state.buttons = buttons;
    state.icon = icon;
    state.result = IDOK;
    
    // Calcular tamaño de diálogo INTELIGENTE según contenido
    int width = 0, height = 0;
    CalculateDialogSize(state.title.c_str(), state.message.c_str(), buttons, width, height);
    
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
        WS_EX_APPWINDOW | WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        DIALOG_CLASS,
        title,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, width, height,
        parent, nullptr, GetModuleHandle(nullptr), &state
    );
    
    if (!hwnd) return IDOK;
    
    // Asegurar que el diálogo sea visible y tenga el foco
    EnableWindow(parent ? parent : GetActiveWindow(), FALSE);
    ShowWindow(hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd);
    UpdateWindow(hwnd);
    
    // Forzar redibujado inmediato
    InvalidateRect(hwnd, nullptr, TRUE);
    UpdateWindow(hwnd);
    
    MSG msg{};
    while (IsWindow(hwnd)) {
        const int gm = GetMessageW(&msg, nullptr, 0, 0);
        if (gm == 0) {
            // GetMessageW devolvió 0 porque consumió un WM_QUIT: reenviarlo
            // para que el bucle principal de la aplicación termine igualmente.
            PostQuitMessage(static_cast<int>(msg.wParam));
            break;
        }
        if (gm == -1) break; // error: evitar un bucle infinito
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    
    // Si el bucle salió por WM_QUIT, cerrar el diálogo que haya quedado abierto
    if (IsWindow(hwnd)) DestroyWindow(hwnd);
    
    EnableWindow(parent ? parent : GetActiveWindow(), TRUE);
    if (parent) SetForegroundWindow(parent);
    
    return state.result;
}

// Inicialización de GDI+
bool InitGDIPlus() {
    const bool ok = GdiplusStartup(&g_state.gdiplusToken, &g_state.gdiplusStartupInput, nullptr) == Ok;
    if (ok) g_ui.Init();
    return ok;
}

// Sistema de tema inteligente
bool DetectSystemDarkMode() {
    // Detectar tema del sistema usando configuración de Windows
    DWORD darkMode = 0;
    DWORD size = sizeof(darkMode);
    HKEY hKey;
    
    // Intentar leer configuración de tema de apps
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExA(hKey, "AppsUseLightTheme", nullptr, nullptr, reinterpret_cast<LPBYTE>(&darkMode), &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return darkMode == 0; // 0 = oscuro, 1 = claro
        }
        RegCloseKey(hKey);
    }
    
    // Fallback: detectar usando registry del sistema
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExA(hKey, "SystemUsesLightTheme", nullptr, nullptr, reinterpret_cast<LPBYTE>(&darkMode), &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return darkMode == 0;
        }
        RegCloseKey(hKey);
    }
    
    // Default: modo oscuro
    return true;
}

void ApplyTheme(ThemeMode theme) {
    bool useDarkTheme = false;
    
    switch (theme) {
        case ThemeMode::Auto:
            useDarkTheme = DetectSystemDarkMode();
            break;
        case ThemeMode::Dark:
            useDarkTheme = true;
            break;
        case ThemeMode::Light:
            useDarkTheme = false;
            break;
    }
    
    g_state.darkModeDetected = useDarkTheme;
    
    if (useDarkTheme) {
        // Aplicar tema oscuro
        BG_COLOR = BG_COLOR_DARK;
        CHECKER_A = CHECKER_A_DARK;
        CHECKER_B = CHECKER_B_DARK;
        GLASS_DOCK_BG = GLASS_DOCK_BG_DARK;
        GLASS_DOCK_BORDER = GLASS_DOCK_BORDER_DARK;
        GLASS_DOCK_SHADOW = GLASS_DOCK_SHADOW_DARK;
        GLASS_BTN_NORMAL = GLASS_BTN_NORMAL_DARK;
        GLASS_BTN_BORDER_NORMAL = GLASS_BTN_BORDER_NORMAL_DARK;
        GLASS_BTN_HOT = GLASS_BTN_HOT_DARK;
        GLASS_BTN_BORDER_HOT = GLASS_BTN_BORDER_HOT_DARK;
        GLASS_BTN_ACTIVE = GLASS_BTN_ACTIVE_DARK;
        if (g_state.hwnd) EnableDarkTitleBar(g_state.hwnd, true);
    } else {
        // Aplicar tema claro
        BG_COLOR = BG_COLOR_LIGHT;
        CHECKER_A = CHECKER_A_LIGHT;
        CHECKER_B = CHECKER_B_LIGHT;
        GLASS_DOCK_BG = GLASS_DOCK_BG_LIGHT;
        GLASS_DOCK_BORDER = GLASS_DOCK_BORDER_LIGHT;
        GLASS_DOCK_SHADOW = GLASS_DOCK_SHADOW_LIGHT;
        GLASS_BTN_NORMAL = GLASS_BTN_NORMAL_LIGHT;
        GLASS_BTN_BORDER_NORMAL = GLASS_BTN_BORDER_NORMAL_LIGHT;
        GLASS_BTN_HOT = GLASS_BTN_HOT_LIGHT;
        GLASS_BTN_BORDER_HOT = GLASS_BTN_BORDER_HOT_LIGHT;
        GLASS_BTN_ACTIVE = GLASS_BTN_ACTIVE_LIGHT;
        if (g_state.hwnd) EnableDarkTitleBar(g_state.hwnd, false);
    }
    
    // Actualizar brush de clase si existe
    if (g_state.classBrush) {
        DeleteObject(g_state.classBrush);
        g_state.classBrush = CreateSolidBrush(BG_COLOR);
    }
}

void InitializeIntelligentTheme() {
    if (!g_state.themeInitialized) {
        g_currentTheme = ThemeMode::Auto;
        ApplyTheme(g_currentTheme);
        g_state.themeInitialized = true;
    }
}

// Sistema de tamaño UI adaptativo
void DetectOptimalUISize() {
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    
    // Calcular DPI de pantalla
    HDC hdc = GetDC(nullptr);
    int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(nullptr, hdc);
    
    // Determinar tamaño óptimo basado en resolución y DPI
    if (screenWidth >= 2560 && screenHeight >= 1440) {
        // Pantallas 4K o grandes
        g_currentUISize = UISize::Large;
    } else if (screenWidth <= 1366 || screenHeight <= 768) {
        // Pantallas pequeñas
        g_currentUISize = UISize::Small;
    } else {
        // Pantallas estándar
        g_currentUISize = UISize::Medium;
    }
    
    // Ajustar según DPI alto
    if (dpiX >= 144) {
        // High DPI (150% o más)
        if (g_currentUISize == UISize::Small) {
            g_currentUISize = UISize::Medium;
        } else if (g_currentUISize == UISize::Medium) {
            g_currentUISize = UISize::Large;
        }
    }
}

void ApplyUISize(UISize size) {
    switch (size) {
        case UISize::Small:
            g_uiScale = UI_SCALE_SMALL;
            break;
        case UISize::Medium:
            g_uiScale = UI_SCALE_MEDIUM;
            break;
        case UISize::Large:
            g_uiScale = UI_SCALE_LARGE;
            break;
    }
}

void InitializeIntelligentUI() {
    // Inicializar tema inteligente
    InitializeIntelligentTheme();
    
    // Detectar y aplicar tamaño óptimo
    DetectOptimalUISize();
    ApplyUISize(g_currentUISize);
}

// Sistema de texto inteligente
TextSizeInfo CalculateOptimalTextSize(const wchar_t* text, int maxWidth, int maxHeight, const wchar_t* fontName) {
    TextSizeInfo info = { 0 };
    info.needsScrolling = false;
    
    if (!text || !text[0]) {
        info.optimalFontSize = 11.0f;
        info.optimalWidth = 400;
        info.optimalHeight = 200;
        info.lineCount = 0;
        return info;
    }
    
    // Crear contexto temporal para medición
    HDC hdc = GetDC(nullptr);
    if (!hdc) {
        info.optimalFontSize = 11.0f;
        info.optimalWidth = 400;
        info.optimalHeight = 200;
        info.lineCount = 1;
        return info;
    }
    
    Graphics graphics(hdc);
    
    // Calcular longitud del texto y estimar líneas
    size_t textLength = wcslen(text);
    int estimatedLines = 1;
    for (size_t i = 0; i < textLength; i++) {
        if (text[i] == L'\n') estimatedLines++;
    }
    
    // Ajustar tamaño de fuente según longitud del texto
    float fontSize = 11.0f; // Base
    if (textLength < 50) {
        fontSize = 14.0f; // Texto corto - fuente más grande
    } else if (textLength < 150) {
        fontSize = 12.0f; // Texto medio
    } else if (textLength < 300) {
        fontSize = 11.0f; // Texto largo
    } else {
        fontSize = 10.0f; // Texto muy largo - fuente más pequeña
    }
    
    // Ajustar según escala UI
    fontSize = fontSize * g_uiScale / 100.0f;
    
    // Medir texto con diferentes tamaños
    FontFamily fontFamily(fontName);
    Font font(&fontFamily, fontSize, FontStyleRegular, UnitPoint);
    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    
    RectF layoutRect(0.0f, 0.0f, static_cast<float>(maxWidth), static_cast<float>(maxHeight));
    RectF boundsRect;
    
    if (graphics.MeasureString(text, -1, &font, layoutRect, &format, &boundsRect) == Ok) {
        info.optimalWidth = static_cast<int>(boundsRect.Width + 80); // Padding
        info.optimalHeight = static_cast<int>(boundsRect.Height + 100); // Espacio para título y botones
        info.lineCount = estimatedLines;
        
        // Verificar si necesita scrolling
        if (boundsRect.Height > maxHeight - 100) {
            info.needsScrolling = true;
            info.optimalHeight = maxHeight;
        }
        
        // Ajustar ancho mínimo y máximo
        info.optimalWidth = std::max(400, std::min(info.optimalWidth, 800));
        info.optimalHeight = std::max(250, std::min(info.optimalHeight, 600));
    } else {
        // Fallback si falla la medición
        info.optimalWidth = 500;
        info.optimalHeight = 300 + (estimatedLines * 20);
        info.lineCount = estimatedLines;
    }
    
    info.optimalFontSize = fontSize;
    
    ReleaseDC(nullptr, hdc);
    return info;
}

void CalculateDialogSize(const wchar_t* title, const wchar_t* message, UINT buttons, int& outWidth, int& outHeight) {
    // Calcular tamaño basado en contenido
    TextSizeInfo titleInfo = CalculateOptimalTextSize(title, 700, 100, L"Segoe UI");
    TextSizeInfo messageInfo = CalculateOptimalTextSize(message, 700, 400, L"Segoe UI");
    
    // Combinar tamaños
    outWidth = std::max(titleInfo.optimalWidth, messageInfo.optimalWidth);
    outHeight = titleInfo.optimalHeight + messageInfo.optimalHeight + 80; // Espacio para botones
    
    // Ajustar según tipo de botones
    if (buttons == MB_YESNO) {
        outWidth = std::max(outWidth, 500); // Mínimo para dos botones
    }
    
    // Ajustar según escala UI
    outWidth = outWidth * g_uiScale / 100;
    outHeight = outHeight * g_uiScale / 100;
    
    // Límites razonables
    outWidth = std::max(400, std::min(outWidth, 900));
    outHeight = std::max(250, std::min(outHeight, 700));
}

float GetAdaptiveFontSize(const wchar_t* text, int availableWidth, int availableHeight, const wchar_t* fontName) {
    (void)fontName;
    if (!text || !text[0]) return 11.0f;
    
    size_t textLength = wcslen(text);
    float baseSize = 11.0f;
    
    // Ajustar según longitud del texto
    if (textLength < 30) {
        baseSize = 14.0f;
    } else if (textLength < 100) {
        baseSize = 12.0f;
    } else if (textLength < 200) {
        baseSize = 11.0f;
    } else {
        baseSize = 10.0f;
    }
    
    // Ajustar según espacio horizontal y vertical disponible
    if (availableWidth < 400 || availableHeight < 300) {
        baseSize *= 0.9f;
    } else if (availableWidth > 600 && availableHeight > 500) {
        baseSize *= 1.1f;
    }
    
    // Ajustar según escala UI
    baseSize = baseSize * g_uiScale / 100.0f;
    
    return baseSize;
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
    GpuShutdown();
    StopPrefetchThread();
    {
        std::lock_guard<std::mutex> lock(g_state.cacheMutex);
        g_state.imageCache.clear();
        g_state.cacheIndex.clear();
        g_state.cacheMemoryBytes = 0;
    }
    FreeCurrentImage();
    FreeDoubleBuffer();
    FreeCheckerTile();
    g_ui.Shutdown();
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
    if (GetExtensionLower(filepath) == L"gif") return false;
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

// Elimina una entrada de la caché (requiere que cacheMutex esté tomado).
// Borra tanto el nodo de la lista LRU como su entrada en cacheIndex.
static void EraseCacheEntry(std::list<CachedImage>::iterator it) {
    if (it == g_state.imageCache.end()) return;
    size_t bytes = 0;
    if (SafePixelBytes(it->width, it->height, bytes)) {
        if (g_state.cacheMemoryBytes >= bytes) g_state.cacheMemoryBytes -= bytes;
        else g_state.cacheMemoryBytes = 0;
    }
    g_state.cacheIndex.erase(CacheKey(it->filepath));
    g_state.imageCache.erase(it);
}

void AddToCache(const std::wstring& filepath, unsigned char* data, int width, int height,
                int channels, int rotation, bool flipH, bool flipV, bool hasAlpha, const std::wstring& decoder) {
    if (!data) return;
    size_t bytes = 0;
    if (!SafePixelBytes(width, height, bytes) || bytes > MAX_CACHE_BYTES) {
        FreePixels(data);
        return;
    }
    std::lock_guard<std::mutex> lock(g_state.cacheMutex);
    const std::wstring key = CacheKey(filepath);
    auto it = g_state.cacheIndex.find(key);
    if (it != g_state.cacheIndex.end()) {
        if (it->second != g_state.imageCache.end()) {
            EraseCacheEntry(it->second); // borra nodo LRU + su índice
        } else {
            g_state.cacheIndex.erase(it); // entrada huérfana: solo el índice
        }
    }

    while (!g_state.imageCache.empty() && (g_state.imageCache.size() >= CACHE_SIZE || (g_state.cacheMemoryBytes + bytes) > MAX_CACHE_BYTES)) {
        EraseCacheEntry(g_state.imageCache.begin());
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
    g_state.cacheMemoryBytes += bytes;
    g_state.cacheIndex[key] = std::prev(g_state.imageCache.end());
}

bool IsInCache(const std::wstring& filepath) {
    std::lock_guard<std::mutex> lock(g_state.cacheMutex);
    return g_state.cacheIndex.find(CacheKey(filepath)) != g_state.cacheIndex.end();
}

void ClearCache() {
    std::lock_guard<std::mutex> lock(g_state.cacheMutex);
    g_state.imageCache.clear();
    g_state.cacheIndex.clear();
    g_state.cacheMemoryBytes = 0;
    TrimProcessMemory();
}

// Descarta de la caché una imagen concreta (p. ej. tras moverla a la papelera)
// sin vaciar el resto de la caché ni romper el precargado de la siguiente.
void RemoveFromCache(const std::wstring& filepath) {
    std::lock_guard<std::mutex> lock(g_state.cacheMutex);
    auto it = g_state.cacheIndex.find(CacheKey(filepath));
    if (it != g_state.cacheIndex.end()) {
        if (it->second != g_state.imageCache.end()) {
            EraseCacheEntry(it->second);
        } else {
            g_state.cacheIndex.erase(it);
        }
    }
}

void FreeCurrentImage() {
    StopGifTimer();
    FreePixels(g_state.imageData);
    g_state.gif.reset();

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

// Repinta únicamente la banda superior donde vive el OSD, no toda la ventana:
// con el repintado por clip del trazador GDI+ el coste es mínimo aunque la
// foto de fondo sea 4K (el OSD ya no re-rasteriza la imagen completa).
void InvalidateOsdRegion() {
    if (!g_state.hwnd) return;
    RECT client{};
    GetClientRect(g_state.hwnd, &client);
    const int cw = client.right - client.left;
    if (cw <= 0) return;
    RECT band{ 4, 6, std::max(4, cw - 4), 54 };
    InvalidateRect(g_state.hwnd, &band, FALSE);
}

// Repinta solo el dock (con margen para la sombra y el antialias), no toda la
// ventana: mover el ratón sobre los botones ya no vuelve a escalar la foto 4K.
void InvalidateDockRegion() {
    if (!g_state.hwnd || g_state.dockRect.right <= g_state.dockRect.left) return;
    RECT inv = g_state.dockRect;
    inv.left = std::max(0L, inv.left - 4);
    inv.top = std::max(0L, inv.top - 4);
    inv.right += 4;
    inv.bottom += 4;
    InvalidateRect(g_state.hwnd, &inv, FALSE);
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
        InvalidateOsdRegion();
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
    std::wstring about = L"ARTPICST — Visor de Imágenes Profesional\n\n";
    about += L"Versión: ";
    about += APP_VERSION_TEXT;
    about += L" (Compilación Estable)\n";
    about += L"Arquitectura: Nativo C++17 para Windows (x64)\n\n";
    about += L"Características principales:\n";
    about += L"  • Motores de descodificación: stb_image, WIC y GDI+.\n";
    about += L"  • Soporte para más de 30 formatos: PNG, JPG, BMP, GIF,\n";
    about += L"    WebP, TIFF, AVIF, HEIC, RAW, HDR, PSD, ICO, etc.\n";
    about += L"  • Caché LRU inteligente y liberación dinámica de recursos.\n";
    about += L"  • Interfaz adaptativa (modo oscuro/claro) con alto DPI.\n\n";
    about += L"Licencia: Software libre y de código abierto.\n";
    about += L"Repositorio: https://github.com/LiebeBlack/Pic\n";
    about += L"Copyright © 2026 ARTPICST. Todos los derechos reservados.";
    ShowThemedMessageBox(hwnd ? hwnd : nullptr, L"Acerca de ARTPICST", about.c_str(), MB_OK, MB_ICONINFORMATION);
}

void ShowProgramInfoDialog(HWND hwnd) {
    std::wstring info;
    info += std::wstring(APP_NAME_TEXT) + L" v" + APP_VERSION_TEXT;
    info += L" — Guía de Controles, Atajos y Funciones\n\n";
    info += L"ATAJOS DE TECLADO:\n";
    info += L"  Flechas Izq / Der : Imagen anterior / siguiente\n";
    info += L"  Espacio / Retroceso : Imagen siguiente / anterior\n";
    info += L"  Inicio / Fin : Primera / última imagen\n";
    info += L"  Rueda / + / - : Acercar / alejar zoom\n";
    info += L"  F : Ajustar imagen a la ventana\n";
    info += L"  1 o 0 : Tamaño real al 100%\n";
    info += L"  D : Alternar modo Ultra-Claridad\n";
    info += L"  R / Shift+R : Girar 90° horario / antihorario\n";
    info += L"  H / V : Volteo horizontal / vertical\n";
    info += L"  G : Alternar escala de grises (B/N)\n";
    info += L"  N : Alternar inversión cromática (Negativo)\n";
    info += L"  T : Alternar tema visual (Oscuro / Claro)\n";
    info += L"  F5 : Iniciar / detener presentación automática\n";
    info += L"  F11 / Doble clic : Pantalla completa (Esc para salir)\n";
    info += L"  Supr : Mover imagen a la Papelera de reciclaje\n";
    info += L"  E / Ctrl+I : Metadatos y propiedades EXIF\n";
    info += L"  Ctrl+S : Guardar / exportar imagen\n";
    info += L"  Ctrl+W : Establecer como fondo de escritorio\n";
    info += L"  Ctrl+E : Mostrar en el Explorador de archivos\n";
    info += L"  Ctrl+C / Ctrl+Shift+C : Copiar imagen / ruta\n";
    info += L"  Ctrl+O / Ctrl+Shift+O : Abrir imagen / carpeta\n";
    info += L"  I : Fijar o alternar barra de información\n\n";
    info += L"CONTROLES DE RATON:\n";
    info += L"  Clic izquierdo + arrastrar : Desplazar imagen\n";
    info += L"  Rueda del ratón : Zoom centrado en el cursor\n";
    info += L"  Clic central : Alternar entre 100% y ajuste\n";
    info += L"  Botones laterales X1/X2 : Anterior / siguiente\n";
    info += L"  Doble clic : Pantalla completa\n\n";
    info += L"PANEL FLOTANTE (DOCK):\n";
    info += L"  Ajustar / 1:1 / Claridad / Rotar / Voltear /\n";
    info += L"  Fondo / Guardar / Pantalla / Abrir\n\n";
    info += L"FORMATOS COMPATIBLES:\n";
    info += L"  JPG, PNG, WebP, HEIC, AVIF, BMP, GIF, TIFF,\n";
    info += L"  ICO, TGA, PSD, HDR, RAW y más de 30 formatos.";

    ShowThemedMessageBox(hwnd, L"Guía de Funciones y Atajos — ARTPICST", info.c_str(), MB_OK, MB_ICONINFORMATION);
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

unsigned char* DecodeWithStb(const std::wstring& filepath, int& width, int& height, int& channels, bool& outHasAlpha, int& autoRotateDeg, bool& outFlipH, bool& outFlipV, GifAnimation* outGif, bool decodeAnimation) {
    width = height = channels = 0;
    outHasAlpha = false;
    autoRotateDeg = 0;
    outFlipH = false;
    outFlipV = false;
    const std::string utf8 = WideToUtf8(filepath);
    if (utf8.empty()) return nullptr;

    const std::wstring ext = GetExtensionLower(filepath);
    const bool isGif = (ext == L"gif");

    if (isGif && decodeAnimation && outGif) {
        int* delays = nullptr;
        int frameCount = 0;
        int tempWidth = 0, tempHeight = 0, tempChannels = 0;
        unsigned char* gifData = nullptr;

        FILE* gifFile = nullptr;
        if (_wfopen_s(&gifFile, filepath.c_str(), L"rb") == 0 && gifFile) {
            if (fseek(gifFile, 0, SEEK_END) == 0) {
                const long gifSize = ftell(gifFile);
                if (gifSize > 0 && gifSize <= MAX_FILE_BYTES && fseek(gifFile, 0, SEEK_SET) == 0) {
                    stbi_uc* gifBuffer = static_cast<stbi_uc*>(malloc(static_cast<size_t>(gifSize)));
                    if (gifBuffer) {
                        const size_t readBytes = fread(gifBuffer, 1, static_cast<size_t>(gifSize), gifFile);
                        if (readBytes == static_cast<size_t>(gifSize)) {
                            gifData = stbi_load_gif_from_memory(
                                gifBuffer, static_cast<int>(gifSize), &delays,
                                &tempWidth, &tempHeight, &frameCount, &tempChannels, 4);
                        }
                        free(gifBuffer);
                    }
                }
            }
            fclose(gifFile);
        }

        if (gifData && frameCount > 1 && tempWidth > 0 && tempHeight > 0) {
            size_t frameBytes = 0;
            if (!SafePixelBytes(tempWidth, tempHeight, frameBytes)) {
                stbi_image_free(gifData);
                if (delays) free(delays);
                return nullptr;
            }

            int keepFrames = frameCount;
            if (keepFrames > MAX_GIF_FRAMES) keepFrames = MAX_GIF_FRAMES;
            while (keepFrames > 1 && (static_cast<size_t>(keepFrames) * frameBytes) > MAX_GIF_BYTES) {
                --keepFrames;
            }

            GifAnimation loaded;
            loaded.frames.reserve(static_cast<size_t>(keepFrames));
            loaded.delaysMs.reserve(static_cast<size_t>(keepFrames));
            bool ok = true;
            for (int i = 0; i < keepFrames; ++i) {
                unsigned char* frameCopy = static_cast<unsigned char*>(malloc(frameBytes));
                if (!frameCopy) {
                    ok = false;
                    break;
                }
                memcpy(frameCopy, gifData + static_cast<size_t>(i) * frameBytes, frameBytes);
                ConvertRGBAtoBGRA(frameCopy, tempWidth, tempHeight, outHasAlpha);
                loaded.frames.push_back(frameCopy);
                const int delay = delays ? delays[i] : 100;
                loaded.delaysMs.push_back(delay);
            }

            stbi_image_free(gifData);
            if (delays) free(delays);

            if (!ok || loaded.frames.empty()) return nullptr;

            unsigned char* first = static_cast<unsigned char*>(malloc(frameBytes));
            if (!first) return nullptr;
            memcpy(first, loaded.frames[0], frameBytes);
            *outGif = std::move(loaded);
            width = tempWidth;
            height = tempHeight;
            channels = 4;
            return first;
        }

        if (gifData) stbi_image_free(gifData);
        if (delays) free(delays);
    }

    unsigned char* pixels = stbi_load(utf8.c_str(), &width, &height, &channels, 4);
    if (!pixels) return nullptr;

    size_t bytes = 0;
    if (!SafePixelBytes(width, height, bytes)) {
        stbi_image_free(pixels);
        width = height = channels = 0;
        return nullptr;
    }

    ConvertRGBAtoBGRA(pixels, width, height, outHasAlpha);

    if (!isGif) {
        // Orientación EXIF completa (rotación + espejos): el motor de renderizado
        // aplica rotación y volteos por separado, así que cada orientación se
        // descompone en su equivalente (rotación, flipH, flipV):
        // 1=normal, 2=espejo H, 3=180°, 4=espejo V, 5=90°+espejo H,
        // 6=90°, 7=270°+espejo H, 8=270° (mismo resultado que el mapa GDI+).
        const int exif = GetExifOrientationFromJpeg(filepath);
        switch (exif) {
            case 2: outFlipH = true; break;
            case 3: autoRotateDeg = 180; break;
            case 4: outFlipV = true; break;
            case 5: autoRotateDeg = 90; outFlipH = true; break;
            case 6: autoRotateDeg = 90; break;
            case 7: autoRotateDeg = 270; outFlipH = true; break;
            case 8: autoRotateDeg = 270; break;
            default: break;
        }
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

    // Verificar si es GIF animado
    GUID containerFormat;
    hr = decoder->GetContainerFormat(&containerFormat);
    bool isGif = (SUCCEEDED(hr) && containerFormat == GUID_ContainerFormatGif);
    
    if (isGif) {
        return nullptr;
    }

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

unsigned char* DecodeImageFile(const std::wstring& filepath, int& width, int& height, int& channels, bool& hasAlpha, int& autoRotateDeg, bool& outFlipH, bool& outFlipV, std::wstring& decoder, GifAnimation* outGif, bool decodeAnimation, bool allowGdiPlus) {
    decoder.clear();
    autoRotateDeg = 0;
    outFlipH = false;
    outFlipV = false;
    unsigned char* pixels = DecodeWithStb(filepath, width, height, channels, hasAlpha, autoRotateDeg, outFlipH, outFlipV, outGif, decodeAnimation);
    if (pixels) {
        decoder = L"stb";
    } else {
        pixels = DecodeWithWic(filepath, width, height, channels, hasAlpha);
        if (pixels) {
            decoder = L"WIC";
        } else if (allowGdiPlus) {
            // GDI+ no es seguro entre hilos: solo se usa desde el hilo de la UI
            // (el hilo de prefetch pasa allowGdiPlus=false).
            pixels = DecodeWithGdiplus(filepath, width, height, channels, hasAlpha);
            if (pixels) {
                decoder = L"GDI+";
            }
        }
    }
    if (pixels) {
        if (!(outGif && outGif->animated())) {
            DownscaleImageIfTooLarge(pixels, width, height);
        }
    }
    return pixels;
}

void StoreCurrentInCache() {
    if (g_state.currentFilePath.empty() || !g_state.imageData) return;
    if (g_state.gif.animated()) return;
    size_t bytes = 0;
    if (!SafePixelBytes(g_state.imageWidth, g_state.imageHeight, bytes) || bytes > MAX_CACHE_BYTES) return;

    // Si la imagen ya está en caché (carga desde caché o rotación/volteo),
    // basta con actualizar sus metadatos EN EL MISMO nodo: no se duplica el
    // buffer de píxeles (hasta ~59 MB por imagen en 4K), que era el motivo de
    // que el consumo de RAM se disparase al navegar o transformar fotos.
    const std::wstring key = CacheKey(g_state.currentFilePath);
    {
        std::lock_guard<std::mutex> lock(g_state.cacheMutex);
        auto it = g_state.cacheIndex.find(key);
        if (it != g_state.cacheIndex.end() && it->second != g_state.imageCache.end()) {
            CachedImage& entry = *it->second;
            entry.rotation = g_state.currentRotation;
            entry.flipH = g_state.currentFlipH;
            entry.flipV = g_state.currentFlipV;
            entry.channels = g_state.imageChannels;
            entry.hasAlpha = g_state.hasAlpha;
            entry.decoder = g_state.decoderName;
            // Mover al extremo MRU de la lista LRU sin copiar píxeles
            g_state.imageCache.splice(g_state.imageCache.end(), g_state.imageCache, it->second);
            it->second = std::prev(g_state.imageCache.end());
            return;
        }
    }

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

    StopGifTimer();

    CachedImage cached;
    if (TryCopyFromCache(filepath, cached) && cached.data) {
        ApplyLoadedImage(cached.data, cached.width, cached.height, cached.channels, cached.rotation,
                         cached.flipH, cached.flipV, cached.hasAlpha, filepath, cached.decoder);
        cached.data = nullptr;
        // Devolver al sistema las páginas del buffer de la imagen anterior:
        // el heap de Windows retiene los bloques libres y la RAM subiría sin parar.
        TrimProcessMemory();
        return true;
    }

    int width = 0, height = 0, channels = 0, autoRotate = 0;
    bool hasAlpha = false, autoFlipH = false, autoFlipV = false;
    std::wstring decoder;
    GifAnimation decodedGif;
    unsigned char* pixels = DecodeImageFile(filepath, width, height, channels, hasAlpha, autoRotate, autoFlipH, autoFlipV, decoder, &decodedGif, true, true);
    if (!pixels) {
        std::wstring msg = L"No se pudo cargar: " + GetFileName(filepath);
        HandleError(msg, false);
        return false;
    }

    ApplyLoadedImage(pixels, width, height, channels, autoRotate, autoFlipH, autoFlipV, hasAlpha, filepath, decoder);
    g_state.gif = std::move(decodedGif);
    StoreCurrentInCache();
    StartGifTimer();
    // Liberar las páginas de los buffers temporales de decodificación (un JPEG
    // de hasta 300 MB se lee entero en memoria) y de la imagen reemplazada, para
    // que el consumo real no se quede anclado en el pico de RAM (~300 MB).
    TrimProcessMemory();
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
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    while (g_state.prefetchRunning) {
        std::unique_lock<std::mutex> lock(g_state.prefetchMutex);
        g_state.prefetchCV.wait_for(lock, std::chrono::milliseconds(500), [] {
            return g_state.prefetchRequested.load() || !g_state.prefetchRunning.load();
        });
        if (!g_state.prefetchRunning) break;
        if (!g_state.prefetchRequested) continue;
        g_state.prefetchRequested = false;
        const size_t targetIndex = g_state.prefetchTargetIndex;
        lock.unlock();

        const uint64_t generation = g_state.folderGeneration.load(std::memory_order_relaxed);
        const size_t count = ImageCount();
        if (count <= 1) continue;
        
        // Precargar ÚNICAMENTE 1 imagen siguiente para conservar RAM y 0% CPU
        const size_t nextIdx = (targetIndex + 1) % count;
        if (!g_state.prefetchRunning) break;
        if (generation != g_state.folderGeneration.load(std::memory_order_relaxed)) continue;
        
        const std::wstring filepath = ImagePathAt(nextIdx);
        if (filepath.empty() || GetExtensionLower(filepath) == L"gif" || IsInCache(filepath)) continue;
        if (!ValidateFileIntegrity(filepath)) continue;
        
        int width = 0, height = 0, channels = 0, autoRotate = 0;
        bool hasAlpha = false, autoFlipH = false, autoFlipV = false;
        std::wstring decoder;
        // allowGdiPlus=false: GDI+ no es seguro entre hilos; el prefetch usa solo
        // stb/WIC, y los formatos que requieren GDI+ se decodifican al navegar.
        unsigned char* pixels = DecodeImageFile(filepath, width, height, channels, hasAlpha, autoRotate, autoFlipH, autoFlipV, decoder, nullptr, false, false);
        if (!pixels) continue;
        if (generation != g_state.folderGeneration.load(std::memory_order_relaxed)) {
            FreePixels(pixels);
            continue;
        }
        AddToCache(filepath, pixels, width, height, channels, autoRotate, autoFlipH, autoFlipV, hasAlpha, decoder);
        
        // Ceder CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
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
    if (g_state.zoomAnimActive) return; // no interfiere con la animación de zoom
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
    CancelZoomAnimation();
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
    CancelZoomAnimation();
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

void CancelZoomAnimation() {
    if (!g_state.zoomAnimActive) return;
    g_state.zoomAnimActive = false;
    if (g_state.hwnd) KillTimer(g_state.hwnd, TIMER_ZOOM);
}

void ZoomAt(float factor, int pivotX, int pivotY) {
    if (!g_state.imageData || !g_state.hwnd) return;
    const float oldZoom = g_state.zoom;
    float newZoom = std::max(MIN_ZOOM, std::min(MAX_ZOOM, oldZoom * factor));
    // Auto-snap a 100% (1.0) cuando esté muy cerca para garantizar píxel perfecto nativo
    if (std::fabs(newZoom - 1.0f) < 0.04f) newZoom = 1.0f;
    if (newZoom == oldZoom) return;
    g_state.fitMode = false;

    // Zoom suave: se anima desde el zoom actual hasta el destino manteniendo
    // el punto bajo el cursor fijo en pantalla (sin saltos bruscos).
    g_state.zoomAnimStart = oldZoom;
    g_state.zoomAnimTarget = newZoom;
    g_state.zoomAnimImageX = (static_cast<float>(pivotX) - g_state.offsetX) / oldZoom;
    g_state.zoomAnimImageY = (static_cast<float>(pivotY) - g_state.offsetY) / oldZoom;
    g_state.zoomAnimStartOffsetX = g_state.offsetX;
    g_state.zoomAnimStartOffsetY = g_state.offsetY;
    g_state.zoomAnimStartTime = GetTickCount();
    g_state.zoomAnimActive = true;
    // ~60 FPS suaves en el zoom animado (antes 8 ms = hasta 125 FPS innecesarios)
    SetTimer(g_state.hwnd, TIMER_ZOOM, 16, nullptr);
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
        { HUD_ONE, L"1:1", 44 }, { HUD_CLARITY, L"Claridad", 68 }, { HUD_ROT, L"Rotar", 50 },
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

// El dock se muestra solo si hubo actividad reciente (el puntero estuvo en la
// franja inferior). Fuente única de verdad para mostrar/ocultar el dock.
bool DockShouldShow() {
    if (!g_state.dockAutoHide) return true;
    return (GetTickCount() - g_state.dockLastActivity) <= DOCK_HIDE_MS;
}

void RenderHud(Graphics& graphics, const RECT& client) {
    LayoutHud(client);

    // Configuración de calidad MÁXIMA para UI
    graphics.SetCompositingQuality(CompositingQualityAssumeLinear);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);

    // Auto-hide dock logic
    const bool shouldShowDock = DockShouldShow();

    // 1. Mensaje OSD / Estado Flotante
    if (g_ui.osdFont && !g_state.statusMessage.empty() &&
        (g_state.osdPinned || GetTickCount() - g_state.osdDisplayTime < OSD_MS)) {
        Font& font = *g_ui.osdFont;
        StringFormat& format = *g_ui.centerFormat;
        RectF layoutRect(0, 0, 600, 40);
        RectF boundRect;
        graphics.MeasureString(g_state.statusMessage.c_str(), -1, &font, layoutRect, &format, &boundRect);

        const float osdW = boundRect.Width + 30.0f;
        const float osdH = 32.0f;
        const float osdX = (client.right - osdW) / 2.0f;
        const float osdY = 14.0f;

        RectF osdRect(osdX, osdY, osdW, osdH);
        GraphicsPath osdPath;
        AddRoundedRect(osdPath, osdRect, 8.0f);

        Color osdBgColor = g_state.darkModeDetected ? Color(245, 24, 25, 30) : Color(245, 255, 255, 255);
        Color osdBorderColor = g_state.darkModeDetected ? Color(255, 60, 62, 74) : Color(255, 210, 214, 222);
        Color osdTextColor = g_state.darkModeDetected ? Color(245, 248, 252) : Color(255, 30, 32, 38);

        SolidBrush osdBg(osdBgColor);
        Pen osdBorder(osdBorderColor, 1.0f);
        graphics.FillPath(&osdBg, &osdPath);
        graphics.DrawPath(&osdBorder, &osdPath);

        SolidBrush textBrush(osdTextColor);
        graphics.DrawString(g_state.statusMessage.c_str(), -1, &font, osdRect, &format, &textBrush);
    }

    // 2. Dock Flotante Compacto y Ultraligero Inferior
    // (durante el arrastre no se redibuja: se repinta al soltar y ahorra CPU)
    if (!g_state.isDragging && shouldShowDock && g_ui.hudBtnFont) {
        RectF dockRectF(static_cast<float>(g_state.dockRect.left),
                       static_cast<float>(g_state.dockRect.top),
                       static_cast<float>(g_state.dockRect.right - g_state.dockRect.left),
                       static_cast<float>(g_state.dockRect.bottom - g_state.dockRect.top));

        GraphicsPath dockPath;
        AddRoundedRect(dockPath, dockRectF, 10.0f);

        // Sombra suave y ligera
        RectF shadowRect = dockRectF;
        shadowRect.Y += 2.0f;
        GraphicsPath shadowPath;
        AddRoundedRect(shadowPath, shadowRect, 10.0f);
        SolidBrush shadowBrush(GLASS_DOCK_SHADOW);
        graphics.FillPath(&shadowBrush, &shadowPath);

        // Fondo y borde
        SolidBrush dockBg(GLASS_DOCK_BG);
        Pen dockBorder(GLASS_DOCK_BORDER, 1.0f);
        graphics.FillPath(&dockBg, &dockPath);
        graphics.DrawPath(&dockBorder, &dockPath);

        // Botones elegantes estilo Windows 10 / Windows 7 con bordes suaves Windows 11
        Font& btnFont = *g_ui.hudBtnFont;
        StringFormat& btnFormat = *g_ui.centerFormat;

        for (int i = 0; i < g_state.hudCount; ++i) {
            const bool hot = (g_state.hud[i].id == g_state.hudHot);
            const bool active = (g_state.hud[i].id == HUD_CLARITY && g_state.effectUltraClarity);

            RectF itemRect(static_cast<float>(g_state.hud[i].rc.left),
                          static_cast<float>(g_state.hud[i].rc.top),
                          static_cast<float>(g_state.hud[i].rc.right - g_state.hud[i].rc.left),
                          static_cast<float>(g_state.hud[i].rc.bottom - g_state.hud[i].rc.top));

            GraphicsPath itemPath;
            AddRoundedRect(itemPath, itemRect, 6.0f);

            if (active) {
                SolidBrush activeBrush(GLASS_BTN_ACTIVE);
                Pen activeBorder(Color(255, 96, 205, 255), 1.2f);
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

            Color btnNormalTextColor = g_state.darkModeDetected ? Color(255, 230, 235, 242) : Color(255, 30, 32, 38);
            SolidBrush btnTextBrush((hot || active) ? Color(255, 255, 255) : btnNormalTextColor);
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

    if (!g_ui.emptyTitleFont || !g_ui.emptyHintFont) return;
    Font& titleFont = *g_ui.emptyTitleFont;
    Font& hintFont = *g_ui.emptyHintFont;

    StringFormat& format = *g_ui.centerFormat;

    RectF titleRect(0.0f, clientRect.bottom * 0.38f - 30.0f, static_cast<float>(clientRect.right), 50.0f);
    RectF hintRect(0.0f, clientRect.bottom * 0.38f + 25.0f, static_cast<float>(clientRect.right), 40.0f);

    Color titleColor = g_state.darkModeDetected ? Color(245, 248, 252) : Color(30, 32, 38);
    Color hintColor = g_state.darkModeDetected ? Color(160, 170, 185) : Color(110, 118, 130);
    SolidBrush titleBrush(titleColor);
    SolidBrush hintBrush(hintColor);

    graphics.DrawString(L"ARTPICST", -1, &titleFont, titleRect, &format, &titleBrush);
    graphics.DrawString(L"Arrastre una imagen aquí  ·  Ctrl + O para abrir  ·  F11 pantalla completa  ·  F1 para ayuda",
                        -1, &hintFont, hintRect, &format, &hintBrush);
}

// Baldosa de ajedrez en caché (32x32, cuadros de 16) con TextureBrush: pinta el
// fondo de transparencia con UNA llamada en vez de ~28.000 FillRectangle por
// fotograma en GDI+. Se reconstruye solo si cambia el tema (colores CHECKER_*).
static Bitmap* s_checkerTile = nullptr;
static TextureBrush* s_checkerBrush = nullptr;
static COLORREF s_checkerCA = 0;
static COLORREF s_checkerCB = 0;

void FreeCheckerTile() {
    delete s_checkerBrush;
    delete s_checkerTile;
    s_checkerBrush = nullptr;
    s_checkerTile = nullptr;
}

void EnsureCheckerTile() {
    const COLORREF ca = CHECKER_A, cb = CHECKER_B;
    if (s_checkerBrush && s_checkerCA == ca && s_checkerCB == cb) return;
    FreeCheckerTile();
    s_checkerCA = ca;
    s_checkerCB = cb;

    const int TILE = 32;
    s_checkerTile = new Bitmap(TILE, TILE, PixelFormat32bppARGB);
    if (!s_checkerTile || s_checkerTile->GetLastStatus() != Ok) {
        FreeCheckerTile();
        return;
    }
    for (int y = 0; y < TILE; ++y) {
        for (int x = 0; x < TILE; ++x) {
            const COLORREF col = (((x / 16) + (y / 16)) & 1) ? ca : cb;
            s_checkerTile->SetPixel(x, y, Color(255, GetRValue(col), GetGValue(col), GetBValue(col)));
        }
    }
    s_checkerBrush = new TextureBrush(s_checkerTile, WrapModeTile);
    if (!s_checkerBrush || s_checkerBrush->GetLastStatus() != Ok) {
        FreeCheckerTile();
    }
}

void DrawCheckerboard(Graphics& g, float x, float y, float w, float h) {
    EnsureCheckerTile();
    if (s_checkerBrush) {
        g.FillRectangle(s_checkerBrush, x, y, w, h);
        return;
    }
    // Respaldo: patrón por rectángulos solo si no se pudo crear la baldosa
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

void RenderImage(const RECT* clipRect = nullptr) {
    if (!g_state.hdcMem || !g_state.hwnd) return;
    RECT rect{};
    GetClientRect(g_state.hwnd, &rect);

    Graphics graphics(g_state.hdcMem);

    // Repintado parcial: solo se re-rasteriza la región sucia (OSD, dock o
    // hover). El doble buffer conserva el resto del fotograma anterior, así
    // que el movimiento del ratón ya no vuelve a escalar la foto 4K completa.
    if (clipRect && clipRect->right > clipRect->left && clipRect->bottom > clipRect->top) {
        graphics.SetClip(Rect(clipRect->left, clipRect->top,
                              clipRect->right - clipRect->left,
                              clipRect->bottom - clipRect->top));
    }

    // Renderizado adaptativo: durante interacción (arrastre o animación de zoom)
    // se usa el modo rápido para mantener 60 FPS; en reposo, máxima calidad.
    const bool interacting = g_state.isDragging || g_state.zoomAnimActive;
    graphics.SetCompositingMode(CompositingModeSourceOver);
    if (interacting) {
        graphics.SetCompositingQuality(CompositingQualityHighSpeed);
        graphics.SetSmoothingMode(SmoothingModeHighSpeed);
        graphics.SetPixelOffsetMode(PixelOffsetModeHalf);
        graphics.SetInterpolationMode(InterpolationModeBilinear);
    } else {
        graphics.SetCompositingQuality(CompositingQualityAssumeLinear);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        const bool pixelPerfectZoom = std::fabs(g_state.zoom - std::round(g_state.zoom)) < 0.01f && g_state.zoom >= 1.0f;
        if (g_state.zoom < 1.0f) {
            // Reducción (foto grande ajustada a la ventana): bilineal — al
            // encoger es visualmente casi idéntica a bicúbica y mucho más ligera.
            graphics.SetInterpolationMode(InterpolationModeBilinear);
            graphics.SetPixelOffsetMode(PixelOffsetModeHalf);
        } else if (pixelPerfectZoom || g_state.zoom > 3.5f) {
            // Píxel perfecto (100%, 200%, etc.) o zoom profundo (> 350%): nitidez cristalina sin borrosidad
            graphics.SetInterpolationMode(InterpolationModeNearestNeighbor);
            graphics.SetPixelOffsetMode(PixelOffsetModeHalf);
        } else {
            // Escalas intermedias: máxima calidad bicúbica fotográfica sin artefactos
            graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        }
    }
    graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    if (!g_state.imageData) {
        RenderEmptyState(graphics, rect);
        RenderHud(graphics, rect);
        return;
    }

    SolidBrush bgBrush(Color(255, GetRValue(BG_COLOR), GetGValue(BG_COLOR), GetBValue(BG_COLOR)));
    graphics.FillRectangle(&bgBrush, 0, 0, rect.right, rect.bottom);

    // Usar frame actual de GIF si está animado
    unsigned char* currentImageData = g_state.imageData;
    if (g_state.gif.animated()) {
        unsigned char* framePtr = g_state.gif.currentFramePixels();
        if (framePtr) currentImageData = framePtr;
    }

    Bitmap bitmap(g_state.imageWidth, g_state.imageHeight,
                  g_state.imageWidth * 4, PixelFormat32bppARGB, currentImageData);

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
    ImageAttributes* pImgAttr = nullptr;

    if (g_state.effectGrayscale) {
        ColorMatrix grayMatrix = {
            0.299f, 0.299f, 0.299f, 0.0f, 0.0f,
            0.587f, 0.587f, 0.587f, 0.0f, 0.0f,
            0.114f, 0.114f, 0.114f, 0.0f, 0.0f,
            0.0f,   0.0f,   0.0f,   1.0f, 0.0f,
            0.0f,   0.0f,   0.0f,   0.0f, 1.0f
        };
        imgAttr.SetColorMatrix(&grayMatrix, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
        imgAttr.SetWrapMode(WrapModeClamp);
        pImgAttr = &imgAttr;
    } else if (g_state.effectInvert) {
        ColorMatrix invMatrix = {
            -1.0f,  0.0f,  0.0f, 0.0f, 0.0f,
             0.0f, -1.0f,  0.0f, 0.0f, 0.0f,
             0.0f,  0.0f, -1.0f, 0.0f, 0.0f,
             0.0f,  0.0f,  0.0f, 1.0f, 0.0f,
             1.0f,  1.0f,  1.0f, 0.0f, 1.0f
        };
        imgAttr.SetColorMatrix(&invMatrix, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
        imgAttr.SetWrapMode(WrapModeClamp);
        pImgAttr = &imgAttr;
    } else if (g_state.effectUltraClarity) {
        const float c = 1.12f;
        const float t = (1.0f - c) / 2.0f;
        ColorMatrix clarityMatrix = {
            c,     0.0f,  0.0f,  0.0f, 0.0f,
            0.0f,  c,     0.0f,  0.0f, 0.0f,
            0.0f,  0.0f,  c,     0.0f, 0.0f,
            0.0f,  0.0f,  0.0f,  1.0f, 0.0f,
            t,     t,     t,     0.0f, 1.0f
        };
        imgAttr.SetColorMatrix(&clarityMatrix, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
        imgAttr.SetWrapMode(WrapModeClamp);
        pImgAttr = &imgAttr;
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

    // Imágenes opacas: copia directa sin blending alfa (más rápido en cada fotograma)
    if (!g_state.hasAlpha) {
        graphics.SetCompositingMode(CompositingModeSourceCopy);
    }
    graphics.DrawImage(&bitmap,
                       RectF(0.0f, 0.0f, g_state.imageWidth * g_state.zoom, g_state.imageHeight * g_state.zoom),
                       0.0f, 0.0f, static_cast<REAL>(g_state.imageWidth), static_cast<REAL>(g_state.imageHeight),
                       UnitPixel, pImgAttr);
    graphics.Restore(state);
    graphics.SetCompositingMode(CompositingModeSourceOver);

    RenderHud(graphics, rect);
}

// ============================================================================
// Renderizador GPU: Direct2D + DirectWrite
// ----------------------------------------------------------------------------
// La escena (foto, tablero de transparencias, OSD, dock y pantalla vacía) se
// dibuja con Direct2D, que escala/interpola la imagen por hardware (GPU o WARP)
// a 60 FPS con CPU ~0%. Los efectos (B/N, negativo, claridad) y el premultipli-
// cado alfa se "hornean" UNA vez por cambio (no por fotograma) en un buffer
// reutilizable y se suben al bitmap de vídeo solo cuando cambian los píxeles
// (carga de imagen o avance de fotograma GIF).
//
// Si Direct2D no está disponible (Windows antiguo, sin GPU y WARP fallido o
// pérdida del dispositivo) el programa vuelve al trazador GDI+ clásico. Se
// puede forzar GDI+ con la variable de entorno ARTPICST_NO_GPU=1.
// ============================================================================

// Efectos horneables (bitset)
const int GPUEFF_GRAY = 1;
const int GPUEFF_INVERT = 2;
const int GPUEFF_CLARITY = 4;

struct GpuState {
    bool factoryCreated = false;
    bool enabled = false;          // target activo y funcionando
    bool attemptFailed = false;    // se desactivó tras un fallo permanente
    bool retryRequested = false;   // reintentar tras un cambio de ventana

    ID2D1Factory* factory = nullptr;
    IDWriteFactory* dwrite = nullptr;
    ID2D1HwndRenderTarget* target = nullptr;

    // Pinceles (dependen del target y del tema activo)
    ID2D1SolidColorBrush* brushBg = nullptr;
    ID2D1SolidColorBrush* brushDockBg = nullptr;
    ID2D1SolidColorBrush* brushDockBorder = nullptr;
    ID2D1SolidColorBrush* brushBtnNormal = nullptr;
    ID2D1SolidColorBrush* brushBtnHot = nullptr;
    ID2D1SolidColorBrush* brushBtnActive = nullptr;
    ID2D1SolidColorBrush* brushTextPrimary = nullptr;
    ID2D1SolidColorBrush* brushTextSecondary = nullptr;
    ID2D1SolidColorBrush* brushTextOnAccent = nullptr;
    ID2D1SolidColorBrush* brushShadow = nullptr;
    ID2D1SolidColorBrush* brushOsdBg = nullptr;
    ID2D1SolidColorBrush* brushOsdBorder = nullptr;

    // Baldosa de ajedrez y su pincel (patrón repetible en GPU)
    ID2D1Bitmap* checkerTile = nullptr;
    ID2D1BitmapBrush* checkerBrush = nullptr;

    // Bitmap de vídeo del fotograma actual + buffer de horneado reutilizable
    ID2D1Bitmap* image = nullptr;
    unsigned char* staging = nullptr;
    size_t stagingBytes = 0;

    int lastUploadW = -1;
    int lastUploadH = -1;
    const unsigned char* lastUploadSrc = nullptr;
    int lastUploadEffects = 0;
    bool lastUploadAlpha = false;
    DWORD themeKey = 0;

    // Formatos de texto DirectWrite (solo dependen de la fábrica)
    IDWriteTextFormat* fmtHud = nullptr;
    IDWriteTextFormat* fmtOsd = nullptr;
    IDWriteTextFormat* fmtTitle = nullptr;
    IDWriteTextFormat* fmtHint = nullptr;
};

GpuState g_gpu;

static D2D1_COLOR_F GpuColorOf(const Gdiplus::Color& c) {
    return D2D1::ColorF(c.GetR() / 255.0f, c.GetG() / 255.0f, c.GetB() / 255.0f, c.GetA() / 255.0f);
}

static D2D1_COLOR_F GpuColorRef(COLORREF c, BYTE alpha = 255) {
    return D2D1::ColorF(GetRValue(c) / 255.0f, GetGValue(c) / 255.0f, GetBValue(c) / 255.0f, alpha / 255.0f);
}

static bool GpuTryInitFactory() {
    if (g_gpu.factoryCreated) return g_gpu.factory != nullptr;
    g_gpu.factoryCreated = true;

    // Escapatoria: ARTPICST_NO_GPU=1 fuerza el trazador GDI+
    wchar_t env[8] = {};
    const DWORD envLen = GetEnvironmentVariableW(L"ARTPICST_NO_GPU", env, 8);
    if (envLen > 0 && envLen < 8 && env[0] == L'1') {
        return false; // factoryCreated=true y factory=null: GDI+ queda fijo
    }

    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_ID2D1Factory,
                                   nullptr, reinterpret_cast<void**>(&g_gpu.factory));
    if (FAILED(hr) || !g_gpu.factory) {
        g_gpu.factory = nullptr;
        return false;
    }
    // dwrite.h del SDK de MS no declara IID_IDWriteFactory (solo el de MinGW);
    // se pasa el GUID canonico de IDWriteFactory, valido en MSVC y MinGW.
    const GUID kIidIDWriteFactory = {0xb859ee5a, 0xd838, 0x4b5b, {0xa2, 0xe8, 0x1a, 0xdc, 0x7d, 0x93, 0xdb, 0x48}};
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, kIidIDWriteFactory,
                                   reinterpret_cast<IUnknown**>(&g_gpu.dwrite))) || !g_gpu.dwrite) {
        g_gpu.dwrite = nullptr;
        if (g_gpu.factory) {
            g_gpu.factory->Release();
            g_gpu.factory = nullptr;
        }
        return false;
    }
    return true;
}

static bool GpuCreateTextFormats() {
    if (!g_gpu.dwrite) return false;
    if (g_gpu.fmtHud && g_gpu.fmtOsd && g_gpu.fmtTitle && g_gpu.fmtHint) return true;

    auto makeFormat = [](FLOAT size, DWRITE_FONT_WEIGHT weight, IDWriteTextFormat** out) -> bool {
        if (*out) return true;
        IDWriteTextFormat* fmt = nullptr;
        if (FAILED(g_gpu.dwrite->CreateTextFormat(L"Segoe UI", nullptr, weight,
                                                  DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                  size, L"es-es", &fmt)) || !fmt) {
            return false;
        }
        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        *out = fmt;
        return true;
    };

    bool ok = true;
    ok = makeFormat(12.0f, DWRITE_FONT_WEIGHT_BOLD, &g_gpu.fmtHud) && ok;
    ok = makeFormat(14.0f, DWRITE_FONT_WEIGHT_BOLD, &g_gpu.fmtOsd) && ok;
    ok = makeFormat(36.0f, DWRITE_FONT_WEIGHT_BOLD, &g_gpu.fmtTitle) && ok;
    ok = makeFormat(14.0f, DWRITE_FONT_WEIGHT_NORMAL, &g_gpu.fmtHint) && ok;
    if (g_gpu.fmtHint) {
        // La pista de la pantalla vacía puede envolver en ventanas estrechas
        g_gpu.fmtHint->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    }
    return ok;
}

static DWORD GpuThemeKey() {
    DWORD key = g_state.darkModeDetected ? 1u : 0u;
    key = key * 31u + static_cast<DWORD>(BG_COLOR);
    key = key * 31u + static_cast<DWORD>(CHECKER_A);
    key = key * 31u + static_cast<DWORD>(CHECKER_B);
    return key;
}

template <typename T>
static void GpuSafeRelease(T*& ptr) {
    if (ptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

// Libera todo lo que depende del target (pinceles, bitmap de imagen, ajedrez)
static void GpuReleaseTargetAssets() {
    GpuSafeRelease(g_gpu.checkerBrush);
    GpuSafeRelease(g_gpu.checkerTile);
    GpuSafeRelease(g_gpu.image);
    GpuSafeRelease(g_gpu.brushOsdBorder);
    GpuSafeRelease(g_gpu.brushOsdBg);
    GpuSafeRelease(g_gpu.brushShadow);
    GpuSafeRelease(g_gpu.brushTextOnAccent);
    GpuSafeRelease(g_gpu.brushTextSecondary);
    GpuSafeRelease(g_gpu.brushTextPrimary);
    GpuSafeRelease(g_gpu.brushBtnActive);
    GpuSafeRelease(g_gpu.brushBtnHot);
    GpuSafeRelease(g_gpu.brushBtnNormal);
    GpuSafeRelease(g_gpu.brushDockBorder);
    GpuSafeRelease(g_gpu.brushDockBg);
    GpuSafeRelease(g_gpu.brushBg);
    g_gpu.lastUploadW = -1;
    g_gpu.lastUploadH = -1;
    g_gpu.lastUploadSrc = nullptr;
    g_gpu.lastUploadEffects = 0;
    g_gpu.lastUploadAlpha = false;
    g_gpu.themeKey = 0;
}

void GpuReleaseAll() {
    GpuReleaseTargetAssets();
    if (g_gpu.target) {
        g_gpu.target->Release();
        g_gpu.target = nullptr;
    }
    g_gpu.enabled = false;
}

void GpuShutdown() {
    GpuReleaseAll();
    if (g_gpu.staging) {
        free(g_gpu.staging);
        g_gpu.staging = nullptr;
        g_gpu.stagingBytes = 0;
    }
    GpuSafeRelease(g_gpu.fmtHint);
    GpuSafeRelease(g_gpu.fmtTitle);
    GpuSafeRelease(g_gpu.fmtOsd);
    GpuSafeRelease(g_gpu.fmtHud);
    if (g_gpu.dwrite) {
        g_gpu.dwrite->Release();
        g_gpu.dwrite = nullptr;
    }
    if (g_gpu.factory) {
        g_gpu.factory->Release();
        g_gpu.factory = nullptr;
    }
    g_gpu.enabled = false;
    g_gpu.factoryCreated = false;
    g_gpu.attemptFailed = false;
}

// (Re)crea el render target del tamaño del cliente. Devuelve true si quedó listo.
static bool GpuEnsureTarget() {
    if (!g_gpu.factory || !g_state.hwnd) return false;

    RECT client{};
    GetClientRect(g_state.hwnd, &client);
    const UINT w = static_cast<UINT>(std::max(0L, client.right - client.left));
    const UINT h = static_cast<UINT>(std::max(0L, client.bottom - client.top));
    if (w == 0 || h == 0) return false;

    D2D1_SIZE_U size = D2D1::SizeU(w, h);
    if (g_gpu.target) {
        const D2D1_SIZE_U cur = g_gpu.target->GetPixelSize();
        if (cur.width == w && cur.height == h) return true;
        GpuReleaseAll(); // cambió el tamaño: recrear en el siguiente intento
    }

    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_HARDWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        96.0f, 96.0f,                       // 1 unidad D2D = 1 píxel (DPIs fijos)
        D2D1_RENDER_TARGET_USAGE_NONE,
        D2D1_FEATURE_LEVEL_DEFAULT);

    HRESULT hr = g_gpu.factory->CreateHwndRenderTarget(
        rtProps,
        D2D1::HwndRenderTargetProperties(g_state.hwnd, size, D2D1_PRESENT_OPTIONS_NONE),
        &g_gpu.target);
    if (FAILED(hr) || !g_gpu.target) {
        g_gpu.target = nullptr;
        return false;
    }
    g_gpu.target->SetDpi(96.0f, 96.0f); // coordenadas = píxeles físicos
    return true;
}

// Crea pinceles y el patrón de ajedrez para el tema activo
static bool GpuEnsureThemeAssets() {
    if (!g_gpu.target) return false;
    const DWORD key = GpuThemeKey();
    if (g_gpu.brushBg && g_gpu.themeKey == key) return true;

    GpuReleaseTargetAssets();
    g_gpu.themeKey = key;

    const bool dark = g_state.darkModeDetected;
    ID2D1RenderTarget* rt = g_gpu.target;

    auto solid = [rt](const D2D1_COLOR_F& color, ID2D1SolidColorBrush** out) -> bool {
        if (*out) return true;
        return SUCCEEDED(rt->CreateSolidColorBrush(color, out));
    };

    const Color dockBg = dark ? GLASS_DOCK_BG_DARK : GLASS_DOCK_BG_LIGHT;
    const Color dockBorder = dark ? GLASS_DOCK_BORDER_DARK : GLASS_DOCK_BORDER_LIGHT;
    const Color btnNormal = dark ? GLASS_BTN_NORMAL_DARK : GLASS_BTN_NORMAL_LIGHT;
    const Color btnHot = dark ? GLASS_BTN_HOT_DARK : GLASS_BTN_HOT_LIGHT;
    const Color btnActive = dark ? GLASS_BTN_ACTIVE_DARK : GLASS_BTN_ACTIVE_LIGHT;
    const Color shadow = dark ? GLASS_DOCK_SHADOW_DARK : GLASS_DOCK_SHADOW_LIGHT;
    const Color osdBg = dark ? Color(245, 24, 25, 30) : Color(245, 255, 255, 255);
    const Color osdBorder = dark ? Color(255, 60, 62, 74) : Color(255, 210, 214, 222);
    const Color textPrimary = dark ? Color(255, 230, 235, 242) : Color(255, 30, 32, 38);
    const Color textSecondary = dark ? Color(255, 165, 175, 190) : Color(255, 105, 113, 125);

    bool ok = true;
    ok = solid(GpuColorRef(BG_COLOR), &g_gpu.brushBg) && ok;
    ok = solid(GpuColorOf(dockBg), &g_gpu.brushDockBg) && ok;
    ok = solid(GpuColorOf(dockBorder), &g_gpu.brushDockBorder) && ok;
    ok = solid(GpuColorOf(btnNormal), &g_gpu.brushBtnNormal) && ok;
    ok = solid(GpuColorOf(btnHot), &g_gpu.brushBtnHot) && ok;
    ok = solid(GpuColorOf(btnActive), &g_gpu.brushBtnActive) && ok;
    ok = solid(GpuColorOf(textPrimary), &g_gpu.brushTextPrimary) && ok;
    ok = solid(GpuColorOf(textSecondary), &g_gpu.brushTextSecondary) && ok;
    ok = solid(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &g_gpu.brushTextOnAccent) && ok;
    ok = solid(GpuColorOf(shadow), &g_gpu.brushShadow) && ok;
    ok = solid(GpuColorOf(osdBg), &g_gpu.brushOsdBg) && ok;
    ok = solid(GpuColorOf(osdBorder), &g_gpu.brushOsdBorder) && ok;

    // Baldosa de ajedrez 32x32 (cuadros de 16) con los colores del tema
    const int TILE = 32;
    std::vector<unsigned char> tile(static_cast<size_t>(TILE) * TILE * 4);
    const COLORREF ca = CHECKER_A;
    const COLORREF cb = CHECKER_B;
    for (int y = 0; y < TILE; ++y) {
        for (int x = 0; x < TILE; ++x) {
            const COLORREF col = (((x / 16) + (y / 16)) & 1) ? cb : ca;
            unsigned char* px = tile.data() + (static_cast<size_t>(y) * TILE + static_cast<size_t>(x)) * 4;
            px[0] = GetBValue(col);
            px[1] = GetGValue(col);
            px[2] = GetRValue(col);
            px[3] = 255;
        }
    }
    if (SUCCEEDED(rt->CreateBitmap(D2D1::SizeU(TILE, TILE), tile.data(), TILE * 4,
                                   D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                                           D2D1_ALPHA_MODE_PREMULTIPLIED)),
                                   &g_gpu.checkerTile))) {
        D2D1_BITMAP_BRUSH_PROPERTIES bmpProps = D2D1::BitmapBrushProperties(
            D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        rt->CreateBitmapBrush(g_gpu.checkerTile, bmpProps, D2D1::BrushProperties(), &g_gpu.checkerBrush);
    }

    if (!ok) {
        GpuReleaseAll();
        return false;
    }
    return true;
}

// Hornea efectos + premultiplicación alfa en el buffer reutilizable (g_gpu.staging)
static unsigned char* GpuBakePixels(unsigned char* src, int w, int h, bool hasAlpha, int effects) {
    const size_t bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
    if (bytes > g_gpu.stagingBytes) {
        unsigned char* bigger = static_cast<unsigned char*>(realloc(g_gpu.staging, bytes));
        if (!bigger) return nullptr;
        g_gpu.staging = bigger;
        g_gpu.stagingBytes = bytes;
    }
    unsigned char* dst = g_gpu.staging;

    for (int y = 0; y < h; ++y) {
        const unsigned char* in = src + static_cast<size_t>(y) * static_cast<size_t>(w) * 4u;
        unsigned char* out = dst + static_cast<size_t>(y) * static_cast<size_t>(w) * 4u;
        for (int x = 0; x < w; ++x) {
            const size_t idx = (static_cast<size_t>(x)) * 4u;
            const unsigned char* p = in + idx;
            unsigned char* q = out + idx;
            int b = p[0], g = p[1], r = p[2];
            const int a = p[3];

            // Efectos en espacio de color (sin alfa): mismo resultado que las
            // matrices GDI+ anteriores, horneado solo cuando cambia el estado.
            if (effects & GPUEFF_GRAY) {
                const int lum = (299 * r + 587 * g + 114 * b) / 1000;
                b = g = r = lum;
            } else if (effects & GPUEFF_INVERT) {
                b = 255 - b; g = 255 - g; r = 255 - r;
            } else if (effects & GPUEFF_CLARITY) {
                const auto conv = [](int v) -> int {
                    float f = 128.0f + (static_cast<float>(v) - 128.0f) * 1.12f;
                    if (f < 0.0f) f = 0.0f;
                    if (f > 255.0f) f = 255.0f;
                    return static_cast<int>(f + 0.5f);
                };
                b = conv(b); g = conv(g); r = conv(r);
            }

            if (hasAlpha && a != 255) {
                // Direct2D requiere alfa premultiplicado
                q[0] = static_cast<unsigned char>((b * a + 127) / 255);
                q[1] = static_cast<unsigned char>((g * a + 127) / 255);
                q[2] = static_cast<unsigned char>((r * a + 127) / 255);
                q[3] = static_cast<unsigned char>(a);
            } else {
                q[0] = static_cast<unsigned char>(b);
                q[1] = static_cast<unsigned char>(g);
                q[2] = static_cast<unsigned char>(r);
                q[3] = static_cast<unsigned char>(a);
            }
        }
    }
    return dst;
}

// Sube (o actualiza) el bitmap del fotograma actual solo cuando cambia algo
static bool GpuUpdateImage() {
    if (!g_gpu.target) return false;
    if (!g_state.imageData) {
        GpuSafeRelease(g_gpu.image);
        return true;
    }

    unsigned char* src = g_state.imageData;
    if (g_state.gif.animated()) {
        unsigned char* frame = g_state.gif.currentFramePixels();
        if (frame) src = frame;
    }

    const int w = g_state.imageWidth;
    const int h = g_state.imageHeight;
    if (w <= 0 || h <= 0) return false;

    const int effects = (g_state.effectGrayscale ? GPUEFF_GRAY : 0) |
                        (g_state.effectInvert ? GPUEFF_INVERT : 0) |
                        (g_state.effectUltraClarity ? GPUEFF_CLARITY : 0);
    const bool alpha = g_state.hasAlpha;

    if (g_gpu.image && w == g_gpu.lastUploadW && h == g_gpu.lastUploadH &&
        src == g_gpu.lastUploadSrc && effects == g_gpu.lastUploadEffects &&
        alpha == g_gpu.lastUploadAlpha) {
        return true; // nada cambió: el bitmap de vídeo sigue siendo válido
    }

    // ¿Hay que transformar píxeles? Solo si hay efecto o transparencia real.
    unsigned char* uploadSrc = src;
    if (effects != 0 || alpha) {
        uploadSrc = GpuBakePixels(src, w, h, alpha, effects);
        if (!uploadSrc) return false;
    }

    const UINT stride = static_cast<UINT>(w) * 4u;
    D2D1_SIZE_U size = D2D1::SizeU(static_cast<UINT>(w), static_cast<UINT>(h));
    if (!g_gpu.image) {
        HRESULT hr = g_gpu.target->CreateBitmap(
            size, uploadSrc, stride,
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                     D2D1_ALPHA_MODE_PREMULTIPLIED)),
            &g_gpu.image);
        if (FAILED(hr) || !g_gpu.image) {
            g_gpu.image = nullptr;
            return false;
        }
    } else if (w != g_gpu.lastUploadW || h != g_gpu.lastUploadH) {
        GpuSafeRelease(g_gpu.image);
        HRESULT hr = g_gpu.target->CreateBitmap(
            size, uploadSrc, stride,
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                     D2D1_ALPHA_MODE_PREMULTIPLIED)),
            &g_gpu.image);
        if (FAILED(hr) || !g_gpu.image) {
            g_gpu.image = nullptr;
            return false;
        }
    } else {
        // Mismo tamaño: actualizar el contenido (fotogramas GIF, efectos)
        if (FAILED(g_gpu.image->CopyFromMemory(nullptr, uploadSrc, stride))) {
            return false;
        }
    }

    g_gpu.lastUploadW = w;
    g_gpu.lastUploadH = h;
    g_gpu.lastUploadSrc = src;
    g_gpu.lastUploadEffects = effects;
    g_gpu.lastUploadAlpha = alpha;

    // El buffer de horneado solo hace falta mientras haya GIF animado (se sube
    // un fotograma cada vez); en el resto, liberarlo para no retener RAM.
    if (!g_state.gif.animated() && g_gpu.staging) {
        free(g_gpu.staging);
        g_gpu.staging = nullptr;
        g_gpu.stagingBytes = 0;
    }
    return true;
}

static void GpuDrawRoundedRect(ID2D1RenderTarget* rt, const D2D1_RECT_F& rc, float radius,
                               ID2D1Brush* fill, ID2D1Brush* border, float borderWidth = 1.0f) {
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(rc, radius, radius);
    if (fill) rt->FillRoundedRectangle(rr, fill);
    if (border && borderWidth > 0.0f) rt->DrawRoundedRectangle(rr, border, borderWidth);
}

static void GpuDrawText(ID2D1RenderTarget* rt, const wchar_t* text, IDWriteTextFormat* fmt,
                        const D2D1_RECT_F& rc, ID2D1Brush* brush) {
    if (!text || !*text || !fmt || !brush) return;
    // Forma de puntero explícito (válida en SDK MSVC y MinGW por igual)
    rt->DrawText(text, static_cast<UINT32>(wcslen(text)), fmt, &rc, brush,
                 D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
}

// Dibuja el fotograma actual con su transformación (zoom, rotación, volteos)
static void GpuDrawImage(ID2D1RenderTarget* rt) {
    if (!g_gpu.image) return;

    int boxW = 0, boxH = 0;
    DisplaySize(boxW, boxH);
    const float zoom = g_state.zoom;
    const float destW = static_cast<float>(boxW) * zoom;
    const float destH = static_cast<float>(boxH) * zoom;

    // Centro en pantalla (respetando el desplazamiento del pan) y centro local
    const float centerX = g_state.offsetX + destW * 0.5f;
    const float centerY = g_state.offsetY + destH * 0.5f;
    const float localCx = static_cast<float>(g_state.imageWidth) * zoom * 0.5f;
    const float localCy = static_cast<float>(g_state.imageHeight) * zoom * 0.5f;

    // Misma composición que el trazador GDI+: T(centro) * R * F * T(-centro local)
    const D2D1_MATRIX_3X2_F m =
        D2D1::Matrix3x2F::Translation(centerX, centerY) *
        D2D1::Matrix3x2F::Rotation(static_cast<float>(g_state.currentRotation)) *
        D2D1::Matrix3x2F::Scale(g_state.currentFlipH ? -1.0f : 1.0f,
                                g_state.currentFlipV ? -1.0f : 1.0f) *
        D2D1::Matrix3x2F::Translation(-localCx, -localCy);
    rt->SetTransform(m);

    const D2D1_RECT_F dst = D2D1::RectF(0.0f, 0.0f,
                                        static_cast<float>(g_state.imageWidth) * zoom,
                                        static_cast<float>(g_state.imageHeight) * zoom);

    // Tablero de ajedrez detrás de imágenes con transparencia
    if (g_state.hasAlpha && g_gpu.checkerBrush) {
        rt->FillRectangle(dst, g_gpu.checkerBrush);
    }

    // Interpolación: vecino más próximo a 100%/200%/… y en zooms profundos;
    // bilineal (GPU) para el resto, con bloqueos al mínimo en reposo.
    const bool pixelPerfect = std::fabs(zoom - std::round(zoom)) < 0.01f && zoom >= 1.0f;
    const D2D1_BITMAP_INTERPOLATION_MODE interp =
        (pixelPerfect || zoom > 3.5f) ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
                                      : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;
    // ID2D1RenderTarget::DrawBitmap acepta como máximo 5 parámetros (la
    // transformación de perspectiva es de ID2D1DeviceContext). Se pasa &dst
    // explícito: forma puntero, válida en MSVC y MinGW.
    rt->DrawBitmap(g_gpu.image, &dst, 1.0f, interp, nullptr);
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
}

static void GpuDrawEmptyState(ID2D1RenderTarget* rt, const RECT& client) {
    if (!g_gpu.fmtTitle || !g_gpu.fmtHint) return;
    const float h = static_cast<float>(client.bottom);
    const float w = static_cast<float>(client.right);
    const D2D1_RECT_F titleRc = D2D1::RectF(0.0f, h * 0.38f - 30.0f, w, h * 0.38f + 20.0f);
    const D2D1_RECT_F hintRc = D2D1::RectF(0.0f, h * 0.38f + 25.0f, w, h * 0.38f + 65.0f);
    GpuDrawText(rt, L"ARTPICST", g_gpu.fmtTitle, titleRc, g_gpu.brushTextPrimary);
    GpuDrawText(rt,
                L"Arrastre una imagen aquí  ·  Ctrl + O para abrir  ·  F11 pantalla completa  ·  F1 para ayuda",
                g_gpu.fmtHint, hintRc, g_gpu.brushTextSecondary);
}

static void GpuDrawOsd(ID2D1RenderTarget* rt, const RECT& client) {
    if (!g_gpu.fmtOsd || g_state.statusMessage.empty()) return;
    if (!g_state.osdPinned && GetTickCount() - g_state.osdDisplayTime >= OSD_MS) return;

    const std::wstring& msg = g_state.statusMessage;
    const float textW = static_cast<float>(msg.size()) * 8.2f + 40.0f;
    const float w = std::max(80.0f, std::min(textW, static_cast<float>(client.right) - 20.0f));
    const float h = 32.0f;
    const float x = (static_cast<float>(client.right) - w) * 0.5f;
    const float y = 14.0f;
    const D2D1_RECT_F rc = D2D1::RectF(x, y, x + w, y + h);
    GpuDrawRoundedRect(rt, rc, 8.0f, g_gpu.brushOsdBg, g_gpu.brushOsdBorder);
    GpuDrawText(rt, msg.c_str(), g_gpu.fmtOsd, rc, g_gpu.brushTextPrimary);
}

static void GpuDrawDock(ID2D1RenderTarget* rt, const RECT& client) {
    if (g_state.isDragging || !DockShouldShow() || !g_gpu.fmtHud) return;

    LayoutHud(client);
    const float left = static_cast<float>(g_state.dockRect.left);
    const float top = static_cast<float>(g_state.dockRect.top);
    const float width = static_cast<float>(g_state.dockRect.right - g_state.dockRect.left);
    const float height = static_cast<float>(g_state.dockRect.bottom - g_state.dockRect.top);

    // Sombra suave
    const D2D1_RECT_F shadowRc = D2D1::RectF(left, top + 2.0f, left + width, top + height + 2.0f);
    GpuDrawRoundedRect(rt, shadowRc, 10.0f, g_gpu.brushShadow, nullptr);

    // Fondo y borde
    const D2D1_RECT_F dockRc = D2D1::RectF(left, top, left + width, top + height);
    GpuDrawRoundedRect(rt, dockRc, 10.0f, g_gpu.brushDockBg, g_gpu.brushDockBorder);

    for (int i = 0; i < g_state.hudCount; ++i) {
        const HudItem& item = g_state.hud[i];
        const bool hot = (item.id == g_state.hudHot);
        const bool active = (item.id == HUD_CLARITY && g_state.effectUltraClarity);
        const D2D1_RECT_F rc = D2D1::RectF(static_cast<float>(item.rc.left),
                                           static_cast<float>(item.rc.top),
                                           static_cast<float>(item.rc.right),
                                           static_cast<float>(item.rc.bottom));

        ID2D1SolidColorBrush* fill = nullptr;
        if (active) {
            fill = g_gpu.brushBtnActive;
        } else if (hot) {
            fill = g_gpu.brushBtnHot;
        } else {
            fill = g_gpu.brushBtnNormal;
        }
        const bool accent = hot || active;
        GpuDrawRoundedRect(rt, rc, 6.0f, fill, accent ? g_gpu.brushBtnActive : g_gpu.brushDockBorder, accent ? 1.2f : 1.0f);
        GpuDrawText(rt, item.label, g_gpu.fmtHud, rc, accent ? g_gpu.brushTextOnAccent : g_gpu.brushTextPrimary);
    }
}

// Dibuja la escena completa por GPU. Devuelve false si hay que usar GDI+.
bool GpuRenderFrame() {
    if (g_gpu.attemptFailed && !g_gpu.retryRequested) return false;
    if (!GpuTryInitFactory()) return false;
    if (!g_gpu.factory) return false;
    g_gpu.retryRequested = false;

    if (!GpuEnsureTarget()) return false;
    if (!GpuCreateTextFormats()) return false;
    if (!GpuEnsureThemeAssets()) return false;
    if (!GpuUpdateImage()) return false;

    ID2D1RenderTarget* rt = g_gpu.target;
    RECT client{};
    GetClientRect(g_state.hwnd, &client);
    const float cw = static_cast<float>(client.right - client.left);
    const float ch = static_cast<float>(client.bottom - client.top);
    if (cw <= 0.0f || ch <= 0.0f) return false;

    rt->BeginDraw();

    // Fondo
    const D2D1_RECT_F bg = D2D1::RectF(0.0f, 0.0f, cw, ch);
    rt->FillRectangle(bg, g_gpu.brushBg);

    if (g_state.imageData && g_gpu.image) {
        GpuDrawImage(rt);
    } else {
        GpuDrawEmptyState(rt, client);
    }

    GpuDrawOsd(rt, client);
    GpuDrawDock(rt, client);

    HRESULT hr = rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        GpuReleaseAll();
        g_gpu.attemptFailed = true;
        return false;
    }
    if (FAILED(hr)) {
        GpuReleaseAll();
        return false;
    }

    if (!g_gpu.enabled) {
        g_gpu.enabled = true;
        // GPU operativo: liberar el doble buffer GDI+ (~8 MB) que ya no se usa
        FreeDoubleBuffer();
    }
    return true;
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
    
    // Configuración de calidad MÁXIMA para exportación
    g.SetCompositingQuality(CompositingQualityAssumeLinear);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    
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
        
        // Configurar calidad JPEG máxima (100%)
        EncoderParameters encoderParams;
        encoderParams.Count = 1;
        encoderParams.Parameter[0].Guid = EncoderQuality;
        encoderParams.Parameter[0].Type = EncoderParameterValueTypeLong;
        encoderParams.Parameter[0].NumberOfValues = 1;
        ULONG quality = 100; // Calidad máxima
        encoderParams.Parameter[0].Value = &quality;
        
        if (output.Save(outPath.c_str(), &encoderClsid, &encoderParams) == Ok) {
            ShowOSD(L"Imagen guardada con éxito (Calidad 100%)");
            return true;
        } else {
            ShowOSD(L"Error al guardar la imagen");
            return false;
        }
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
    ShowThemedMessageBox(hwnd, L"Propiedades y Metadatos — ARTPICST", info.c_str(), MB_OK, MB_ICONINFORMATION);
}

void ToggleTheme() {
    g_currentTheme = (g_state.darkModeDetected) ? ThemeMode::Light : ThemeMode::Dark;
    ApplyTheme(g_currentTheme);
    ShowOSD(g_state.darkModeDetected ? L"Tema: Modo Oscuro" : L"Tema: Modo Claro");
    InvalidateRect(g_state.hwnd, nullptr, FALSE);
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
        
        // Configuración de calidad MÁXIMA para portapapeles
        g.SetCompositingQuality(CompositingQualityAssumeLinear);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        
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
    ShowOSD(L"Imagen copiada al portapapeles (Calidad Máxima)");
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
    EnableDarkTitleBar(hwnd, g_state.darkModeDetected);
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
        // Quitar de la lista SIN mantener filesMutex mientras se carga la imagen
        // siguiente: LoadImageFromPath -> UpdateWindowTitle -> ImageCount vuelve a
        // bloquear filesMutex (mutex no recursivo), lo que antes era un deadlock.
        std::wstring nextPath;
        bool removed = false;
        {
            std::lock_guard<std::mutex> lock(g_state.filesMutex);
            auto it = std::find_if(g_state.imageFiles.begin(), g_state.imageFiles.end(),
                                   [&path](const std::wstring& f) { return PathsEqualCaseInsensitive(f, path); });
            if (it != g_state.imageFiles.end()) {
                size_t idx = std::distance(g_state.imageFiles.begin(), it);
                g_state.imageFiles.erase(it);
                removed = true;
                if (!g_state.imageFiles.empty()) {
                    if (idx >= g_state.imageFiles.size()) idx = g_state.imageFiles.size() - 1;
                    g_state.currentImageIndex = idx;
                    nextPath = g_state.imageFiles[idx];
                }
            }
        }
        if (removed) {
            // La imagen ya no existe en disco: descartar su copia en caché
            RemoveFromCache(path);
            if (g_state.imageFiles.empty()) {
                FreeCurrentImage();
                UpdateWindowTitle();
            } else if (!nextPath.empty()) {
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
        case 19: SetAsWallpaper(); break;
        case 20: ToggleGrayscale(); break;
        case 21: ToggleInvert(); break;
        case 22: ShowExifDialog(hwnd); break;
        case 23: ShowProgramInfoDialog(hwnd); break;
        case 24: ToggleUltraClarity(); break;
        default: break;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static bool s_trackingMouse = false;
    switch (msg) {
        case WM_SETCURSOR:
            // Cursor de mano sobre los botones del dock (solo si está visible)
            if (LOWORD(lParam) == HTCLIENT && DockShouldShow() && g_state.hudHot != HUD_NONE) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        case WM_CREATE: {
            g_state.hwnd = hwnd;
            DragAcceptFiles(hwnd, TRUE);
            EnableDarkTitleBar(hwnd, g_state.darkModeDetected);
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
            if (wParam == SIZE_MINIMIZED) {
                CancelZoomAnimation();
                GpuReleaseAll(); // liberar recursos de vídeo al minimizar
                FreeDoubleBuffer();
                TrimProcessMemory();
                return 0;
            }
            g_gpu.retryRequested = true; // nuevo tamaño: reintentar GPU si falló
            const int width = LOWORD(lParam);
            const int height = HIWORD(lParam);
            if (g_gpu.enabled) {
                // GPU activo: no se usa el doble buffer GDI+ (ahorra ~8 MB de RAM)
                FreeDoubleBuffer();
            } else {
                CreateDoubleBuffer(width, height);
            }
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
            
            // Trazado GPU (Direct2D): si funciona, toda la escena se dibuja por
            // hardware (60 FPS, CPU ~0%) y no se usa el buffer GDI+.
            if (GpuRenderFrame()) {
                EndPaint(hwnd, &ps);
                return 0;
            }
            
            if (!g_state.hdcMem) {
                RECT client{};
                GetClientRect(hwnd, &client);
                CreateDoubleBuffer(client.right, client.bottom);
            }
            if (g_state.hdcMem) {
                RenderImage(&ps.rcPaint);
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
                        // ShowOSD ya repinta solo la banda del OSD
                        g_state.osdPinned = !g_state.osdPinned;
                        ShowOSD(g_state.osdPinned ? L"Info fija" : L"Info auto");
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
            CancelZoomAnimation();
            g_state.isDragging = true;
            g_state.dragStartX = x;
            g_state.dragStartY = y;
            g_state.dragStartOffsetX = g_state.offsetX;
            g_state.dragStartOffsetY = g_state.offsetY;
            SetCapture(hwnd);
            return 0;
        }
        case WM_LBUTTONUP:
            if (g_state.isDragging) {
                g_state.isDragging = false;
                ReleaseCapture();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
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
            if (!s_trackingMouse) {
                TRACKMOUSEEVENT tme{};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
                s_trackingMouse = true;
            }
            
            // Dock proximity detection
            RECT client{};
            GetClientRect(hwnd, &client);
            const int distanceFromBottom = client.bottom - y;
            
            if (distanceFromBottom < DOCK_PROXIMITY_THRESHOLD) {
                const bool wasHidden = !DockShouldShow();
                g_state.dockLastActivity = GetTickCount();
                if (wasHidden) InvalidateDockRegion();
                KillTimer(hwnd, TIMER_DOCK_HIDE);
                SetTimer(hwnd, TIMER_DOCK_HIDE, DOCK_HIDE_MS, nullptr);
            }
            
            const HudId hot = HitTestHud(x, y);
            if (hot != g_state.hudHot) {
                g_state.hudHot = hot;
                InvalidateDockRegion();
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
        case WM_MOUSELEAVE: {
            s_trackingMouse = false;
            if (g_state.hudHot != HUD_NONE) {
                g_state.hudHot = HUD_NONE;
                InvalidateDockRegion();
            }
            return 0;
        }
        case WM_TIMER:
            if (wParam == TIMER_OSD) {
                KillTimer(hwnd, TIMER_OSD);
                if (!g_state.osdPinned) {
                    g_state.statusMessage.clear();
                }
                g_state.showOSD = false;
                InvalidateOsdRegion();
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
                    const bool wasVisible = DockShouldShow();
                    // Expirar la actividad: el dock queda oculto hasta que el
                    // puntero vuelva a la franja inferior (equivale a ocultarlo)
                    g_state.dockLastActivity = GetTickCount() - DOCK_HIDE_MS;
                    KillTimer(hwnd, TIMER_DOCK_HIDE);
                    if (wasVisible) InvalidateDockRegion();
                } else {
                    g_state.dockLastActivity = GetTickCount();
                }
            } else if (wParam == TIMER_GIF) {
                if (g_state.gif.animated()) {
                    // Avance proporcional al tiempo transcurrido: si el sistema va
                    // retrasado se saltan fotogramas para mantener la animación en sync.
                    const int frameCount = g_state.gif.frameCount();
                    const int delay = g_state.gif.delayAt(g_state.gif.current);
                    const DWORD now = GetTickCount();
                    int advance = static_cast<int>((now - g_state.gifLastTick) / std::max(delay, 1));
                    if (advance < 1) advance = 1;
                    if (advance > frameCount) advance = frameCount;
                    g_state.gif.current = (g_state.gif.current + advance) % frameCount;
                    g_state.gifLastTick = now;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    SetTimer(hwnd, TIMER_GIF, static_cast<UINT>(g_state.gif.delayAt(g_state.gif.current)), nullptr);
                } else {
                    KillTimer(hwnd, TIMER_GIF);
                }
            } else if (wParam == TIMER_ZOOM) {
                if (!g_state.zoomAnimActive) {
                    KillTimer(hwnd, TIMER_ZOOM);
                } else {
                    const DWORD now = GetTickCount();
                    float t = static_cast<float>(now - g_state.zoomAnimStartTime) / static_cast<float>(ZOOM_ANIM_MS);
                    if (t >= 1.0f) t = 1.0f;
                    // smoothstep: arranque y frenado suaves
                    const float eased = t * t * (3.0f - 2.0f * t);
                    g_state.zoom = g_state.zoomAnimStart + (g_state.zoomAnimTarget - g_state.zoomAnimStart) * eased;
                    g_state.offsetX = g_state.zoomAnimStartOffsetX + g_state.zoomAnimImageX * (g_state.zoomAnimStart - g_state.zoom);
                    g_state.offsetY = g_state.zoomAnimStartOffsetY + g_state.zoomAnimImageY * (g_state.zoomAnimStart - g_state.zoom);
                    if (t >= 1.0f) {
                        g_state.zoom = g_state.zoomAnimTarget;
                        g_state.zoomAnimActive = false;
                        KillTimer(hwnd, TIMER_ZOOM);
                        EnsureImageVisible();
                        wchar_t zoomText[64];
                        swprintf_s(zoomText, L"Zoom: %d%%", static_cast<int>(g_state.zoom * 100.0f + 0.5f));
                        ShowOSD(zoomText);
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        case WM_DROPFILES: {
            HDROP hDrop = reinterpret_cast<HDROP>(wParam);
            const UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
            if (fileCount > 0) {
                const UINT chars = DragQueryFileW(hDrop, 0, nullptr, 0);
                // El buffer debe alojar chars + el terminador nulo que escribe DragQueryFileW
                std::wstring filePath(chars + 1, L'\0');
                const UINT written = DragQueryFileW(hDrop, 0, filePath.data(), chars + 1);
                if (written > 0) {
                    filePath.resize(written);
                    OpenPath(filePath);
                }
            }
            DragFinish(hDrop);
            return 0;
        }
        case WM_DESTROY:
            GpuShutdown();
            KillTimer(hwnd, TIMER_OSD);
            KillTimer(hwnd, TIMER_SLIDESHOW);
            KillTimer(hwnd, TIMER_DOCK_HIDE);
            KillTimer(hwnd, TIMER_GIF);
            KillTimer(hwnd, TIMER_ZOOM);
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
            const bool ok = RegisterFileAssociationForCurrentUser();
            if (ok) {
                LogMessage(L"Asociaciones de archivo registradas (HKCU).");
            } else {
                LogMessage(L"Error al registrar las asociaciones de archivo.");
            }
            g_exitAfterCommandLine = true;
            LocalFree(argv);
            return;
        }
        if (arg == L"--unregister") {
            UnregisterFileAssociationForCurrentUser();
            LogMessage(L"Asociaciones de archivo eliminadas (HKCU).");
            g_exitAfterCommandLine = true;
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
    
    // Inicializar sistemas inteligentes
    InitializeIntelligentUI();

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

    if (g_exitAfterCommandLine) {
        // Modo consola: --register / --unregister ya hicieron su trabajo.
        CleanupGDIPlus();
        if (SUCCEEDED(g_state.comHr)) CoUninitialize();
        return 0;
    }

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

    EnableDarkTitleBar(hwnd, g_state.darkModeDetected);
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
