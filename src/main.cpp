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
#ifndef STBI_WINDOWS_UTF8
#define STBI_WINDOWS_UTF8
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <gdiplus.h>

#include <algorithm>
#include <atomic>
#include <chrono>
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

const wchar_t CLASS_NAME[] = L"ARTPICSTWindow";
const COLORREF BG_COLOR = RGB(18, 18, 18);
const COLORREF BAR_COLOR = RGB(28, 28, 30);
const COLORREF ACCENT_COLOR = RGB(90, 160, 255);
const float MIN_ZOOM = 0.05f;
const float MAX_ZOOM = 64.0f;
const float ZOOM_STEP = 1.15f;
const size_t CACHE_SIZE = 5;
const int MAX_DIMENSION = 20000;
const LONGLONG MAX_FILE_BYTES = 500LL * 1024LL * 1024LL;
const UINT OSD_MS = 4000;
const int HUD_HEIGHT = 52;
const UINT_PTR TIMER_OSD = 1;

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

struct CachedImage {
    unsigned char* data = nullptr;
    int width = 0;
    int height = 0;
    int channels = 0;
    int rotation = 0;
    std::wstring filepath;
    std::wstring decoder;

    CachedImage() = default;
    ~CachedImage() {
        if (data) {
            stbi_image_free(data);
            data = nullptr;
        }
    }
    CachedImage(const CachedImage&) = delete;
    CachedImage& operator=(const CachedImage&) = delete;
    CachedImage(CachedImage&& other) noexcept
        : data(other.data), width(other.width), height(other.height),
          channels(other.channels), rotation(other.rotation),
          filepath(std::move(other.filepath)), decoder(std::move(other.decoder)) {
        other.data = nullptr;
        other.width = other.height = other.channels = other.rotation = 0;
    }
    CachedImage& operator=(CachedImage&& other) noexcept {
        if (this != &other) {
            if (data) stbi_image_free(data);
            data = other.data;
            width = other.width;
            height = other.height;
            channels = other.channels;
            rotation = other.rotation;
            filepath = std::move(other.filepath);
            decoder = std::move(other.decoder);
            other.data = nullptr;
            other.width = other.height = other.channels = other.rotation = 0;
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
    std::wstring currentFilePath;
    std::wstring decoderName;

    std::vector<std::wstring> imageFiles;
    size_t currentImageIndex = 0;
    std::wstring currentFolder;
    std::wstring startupFilePath;
    std::mutex filesMutex;

    float zoom = 1.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    bool fitMode = true;

    bool isDragging = false;
    int dragStartX = 0;
    int dragStartY = 0;
    float dragStartOffsetX = 0.0f;
    float dragStartOffsetY = 0.0f;

    std::unordered_map<std::wstring, std::list<CachedImage>::iterator> cacheIndex;
    std::list<CachedImage> imageCache;
    std::mutex cacheMutex;

    std::thread prefetchThread;
    std::atomic<bool> prefetchRunning{false};
    std::atomic<bool> prefetchRequested{false};
    std::condition_variable prefetchCV;
    std::mutex prefetchMutex;
    size_t prefetchTargetIndex = 0;
    std::atomic<uint64_t> folderGeneration{0};

    bool showOSD = true;
    bool osdPinned = true;
    DWORD osdDisplayTime = 0;
    std::wstring statusMessage;

    bool isFullscreen = false;
    WindowMode windowMode = WindowMode::Normal;
    WINDOWPLACEMENT windowedPlacement{};
    LONG windowedStyle = 0;
    LONG windowedExStyle = 0;

    HudItem hud[8]{};
    int hudCount = 0;
    HudId hudHot = HUD_NONE;

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
        if (imageData) {
            stbi_image_free(imageData);
            imageData = nullptr;
        }
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
void ConvertRGBAtoBGRA(unsigned char* pixels, int width, int height);
void EnsureImageVisible();
void FitImageToWindow(int windowWidth, int windowHeight);
void RotateImage();
void ToggleFullscreen();
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
bool OpenImageFileDialog(HWND hwnd);
bool OpenFolderDialog(HWND hwnd);
void LayoutHud(const RECT& client);
HudId HitTestHud(int x, int y);
void InvokeHud(HudId id);
std::wstring WideToLower(std::wstring value);
std::string WideToUtf8(const std::wstring& value);
void ApplyWindowMode(HWND hwnd, WindowMode mode);
std::wstring NormalizePath(const std::wstring& path);
void ZoomAt(float factor, int pivotX, int pivotY);

static bool SafePixelBytes(int width, int height, size_t& outBytes) {
    if (width <= 0 || height <= 0) return false;
    if (width > MAX_DIMENSION || height > MAX_DIMENSION) return false;
    const uint64_t bytes = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4ull;
    if (bytes > static_cast<uint64_t>(SIZE_MAX)) return false;
    outBytes = static_cast<size_t>(bytes);
    return true;
}

static int ViewHeight(int clientHeight) {
    return std::max(1, clientHeight - HUD_HEIGHT);
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
    if (!value.empty()) {
        CharLowerBuffW(value.data(), static_cast<DWORD>(value.size()));
    }
    return value;
}

bool PathsEqualCaseInsensitive(const std::wstring& a, const std::wstring& b) {
    return WideToLower(a) == WideToLower(b);
}

std::wstring CacheKey(const std::wstring& path) {
    return WideToLower(path);
}

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

bool InitGDIPlus() {
    return GdiplusStartup(&g_state.gdiplusToken, &g_state.gdiplusStartupInput, nullptr) == Ok;
}

void EnableDarkTitleBar(HWND hwnd) {
    if (!hwnd) return;
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
}

void FreeDoubleBuffer() {
    if (!g_state.hdcMem) return;
    if (g_state.hbmOld) {
        SelectObject(g_state.hdcMem, g_state.hbmOld);
        g_state.hbmOld = nullptr;
    }
    if (g_state.hbmMem) {
        DeleteObject(g_state.hbmMem);
        g_state.hbmMem = nullptr;
    }
    DeleteDC(g_state.hdcMem);
    g_state.hdcMem = nullptr;
}

void StopPrefetchThread() {
    g_state.prefetchRunning = false;
    g_state.prefetchRequested = true;
    g_state.prefetchCV.notify_all();
    if (g_state.prefetchThread.joinable()) {
        g_state.prefetchThread.join();
    }
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
    outCopy.filepath = src.filepath;
    outCopy.decoder = src.decoder;
    return true;
}

void AddToCache(const std::wstring& filepath, unsigned char* data, int width, int height,
                int channels, int rotation, const std::wstring& decoder) {
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
    if (!status.empty()) g_state.statusMessage = status;
    if (g_state.hwnd && !g_state.osdPinned) {
        SetTimer(g_state.hwnd, TIMER_OSD, OSD_MS + 80, nullptr);
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
            MessageBoxW(g_state.hwnd, errorMsg.c_str(), L"ARTPICST", MB_OK | MB_ICONWARNING);
        }
    }
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
    std::sort(found.begin(), found.end(), [](const std::wstring& a, const std::wstring& b) {
        return WideToLower(a) < WideToLower(b);
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

unsigned char* DecodeWithStb(const std::wstring& filepath, int& width, int& height, int& channels) {
    width = height = channels = 0;
    const std::string utf8 = WideToUtf8(filepath);
    if (utf8.empty()) return nullptr;
    unsigned char* pixels = stbi_load(utf8.c_str(), &width, &height, &channels, 4);
    if (!pixels) return nullptr;
    size_t bytes = 0;
    if (!SafePixelBytes(width, height, bytes)) {
        stbi_image_free(pixels);
        width = height = channels = 0;
        return nullptr;
    }
    ConvertRGBAtoBGRA(pixels, width, height);
    return pixels;
}

unsigned char* DecodeWithWic(const std::wstring& filepath, int& width, int& height, int& channels) {
    width = height = channels = 0;
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

    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr) || !converter) return nullptr;

    hr = converter->Initialize(frame.get(), GUID_WICPixelFormat32bppBGRA,
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
    return pixels;
}

unsigned char* DecodeWithGdiplus(const std::wstring& filepath, int& width, int& height, int& channels) {
    width = height = channels = 0;
    Bitmap bmp(filepath.c_str());
    if (bmp.GetLastStatus() != Ok) return nullptr;
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
    return pixels;
}

unsigned char* DecodeImageFile(const std::wstring& filepath, int& width, int& height, int& channels, std::wstring& decoder) {
    decoder.clear();
    unsigned char* pixels = DecodeWithStb(filepath, width, height, channels);
    if (pixels) {
        decoder = L"stb";
        return pixels;
    }
    pixels = DecodeWithWic(filepath, width, height, channels);
    if (pixels) {
        decoder = L"WIC";
        return pixels;
    }
    pixels = DecodeWithGdiplus(filepath, width, height, channels);
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
               g_state.imageChannels, g_state.currentRotation, g_state.decoderName);
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
                      const std::wstring& filepath, const std::wstring& decoder) {
    FreeCurrentImage();
    g_state.imageData = pixels;
    g_state.imageWidth = width;
    g_state.imageHeight = height;
    g_state.imageChannels = channels;
    g_state.currentRotation = rotation;
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
                         filepath, cached.decoder);
        cached.data = nullptr;
        return true;
    }

    int width = 0, height = 0, channels = 0;
    std::wstring decoder;
    unsigned char* pixels = DecodeImageFile(filepath, width, height, channels, decoder);
    if (!pixels) {
        std::wstring msg = L"No se pudo cargar: " + GetFileName(filepath);
        HandleError(msg, false);
        return false;
    }

    ApplyLoadedImage(pixels, width, height, channels, 0, filepath, decoder);
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
            int width = 0, height = 0, channels = 0;
            std::wstring decoder;
            unsigned char* pixels = DecodeImageFile(filepath, width, height, channels, decoder);
            if (!pixels) continue;
            if (generation != g_state.folderGeneration.load(std::memory_order_relaxed)) {
                stbi_image_free(pixels);
                break;
            }
            AddToCache(filepath, pixels, width, height, channels, 0, decoder);
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

void RotateImage() {
    if (!g_state.imageData) return;
    g_state.currentRotation = (g_state.currentRotation + 90) % 360;
    if (g_state.hwnd) {
        RECT client{};
        GetClientRect(g_state.hwnd, &client);
        FitImageToWindow(client.right - client.left, client.bottom - client.top);
    }
    StoreCurrentInCache();
    ShowOSD(L"Rotada");
    InvalidateRect(g_state.hwnd, nullptr, FALSE);
}

void ConvertRGBAtoBGRA(unsigned char* pixels, int width, int height) {
    if (!pixels || width <= 0 || height <= 0) return;
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
    for (size_t i = 0; i < count; ++i) {
        unsigned char* p = pixels + (i * 4);
        std::swap(p[0], p[2]);
    }
}

void EnsureImageVisible() {
    if (!g_state.imageData || !g_state.hwnd) return;
    RECT client{};
    GetClientRect(g_state.hwnd, &client);
    const int windowWidth = client.right - client.left;
    const int windowHeight = ViewHeight(client.bottom - client.top);
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
    windowHeight = ViewHeight(windowHeight);
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
    g_state.offsetY = (ViewHeight(client.bottom) - imageHeight) * 0.5f;
    EnsureImageVisible();
    ShowOSD(L"100%");
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

std::wstring GetFileSizeString(const std::wstring& filepath) {
    WIN32_FILE_ATTRIBUTE_DATA fileData{};
    if (!GetFileAttributesExW(filepath.c_str(), GetFileExInfoStandard, &fileData)) return L"?";
    LARGE_INTEGER size{};
    size.HighPart = fileData.nFileSizeHigh;
    size.LowPart = fileData.nFileSizeLow;
    const wchar_t* units[] = { L"B", L"KB", L"MB", L"GB" };
    int unit = 0;
    double value = static_cast<double>(size.QuadPart);
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }
    wchar_t buffer[32];
    swprintf_s(buffer, L"%.1f %s", value, units[unit]);
    return buffer;
}

std::wstring GetFileName(const std::wstring& filepath) {
    const size_t pos = filepath.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return filepath;
    return filepath.substr(pos + 1);
}

void LayoutHud(const RECT& client) {
    const int y = client.bottom - HUD_HEIGHT + 10;
    const int h = HUD_HEIGHT - 18;
    const int gap = 8;
    int x = 12;
    auto add = [&](HudId id, const wchar_t* label, int w) {
        if (g_state.hudCount >= 8) return;
        HudItem& item = g_state.hud[g_state.hudCount++];
        item.id = id;
        item.label = label;
        item.rc = { x, y, x + w, y + h };
        x += w + gap;
    };
    g_state.hudCount = 0;
    add(HUD_PREV, L"◀", 44);
    add(HUD_NEXT, L"▶", 44);
    add(HUD_FIT, L"Ajustar", 78);
    add(HUD_ONE, L"100%", 58);
    add(HUD_ROT, L"Rotar", 64);
    add(HUD_FULL, g_state.isFullscreen ? L"Ventana" : L"Pantalla", 88);
    add(HUD_OPEN, L"Abrir", 64);
}

HudId HitTestHud(int x, int y) {
    if (g_state.hwnd) {
        RECT client{};
        GetClientRect(g_state.hwnd, &client);
        LayoutHud(client);
    }
    for (int i = 0; i < g_state.hudCount; ++i) {
        if (PtInRect(&g_state.hud[i].rc, POINT{ x, y })) return g_state.hud[i].id;
    }
    return HUD_NONE;
}

void DrawRoundish(HDC hdc, const RECT& rc, COLORREF fill, COLORREF border) {
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, brush));
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 8, 8);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void RenderHud(HDC hdc, const RECT& client) {
    LayoutHud(client);
    RECT bar = { 0, client.bottom - HUD_HEIGHT, client.right, client.bottom };
    HBRUSH barBrush = CreateSolidBrush(BAR_COLOR);
    FillRect(hdc, &bar, barBrush);
    DeleteObject(barBrush);

    HPEN line = CreatePen(PS_SOLID, 1, RGB(48, 48, 52));
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, line));
    MoveToEx(hdc, 0, bar.top, nullptr);
    LineTo(hdc, client.right, bar.top);
    SelectObject(hdc, oldPen);
    DeleteObject(line);

    HFONT hFont = CreateFontW(15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT hOld = static_cast<HFONT>(SelectObject(hdc, hFont));
    SetBkMode(hdc, TRANSPARENT);

    for (int i = 0; i < g_state.hudCount; ++i) {
        const bool hot = g_state.hud[i].id == g_state.hudHot;
        DrawRoundish(hdc, g_state.hud[i].rc,
                     hot ? RGB(50, 80, 120) : RGB(42, 42, 46),
                     hot ? ACCENT_COLOR : RGB(64, 64, 70));
        SetTextColor(hdc, hot ? RGB(255, 255, 255) : RGB(230, 230, 230));
        DrawTextW(hdc, g_state.hud[i].label, -1, &g_state.hud[i].rc,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    RECT info = { 12, bar.top, client.right - 12, bar.bottom };
    if (g_state.hudCount > 0) {
        info.left = g_state.hud[g_state.hudCount - 1].rc.right + 16;
    }
    if (info.left < info.right - 20) {
        SetTextColor(hdc, RGB(180, 180, 185));
        std::wstring text;
        if (g_state.imageData && !g_state.currentFilePath.empty()) {
            int dw = 0, dh = 0;
            DisplaySize(dw, dh);
            wchar_t extra[192];
            swprintf_s(extra, L"%s   %dx%d   %s   %.0f%%   %zu/%zu   %s",
                       GetFileName(g_state.currentFilePath).c_str(), dw, dh,
                       GetFileSizeString(g_state.currentFilePath).c_str(),
                       g_state.zoom * 100.0f,
                       g_state.currentImageIndex + 1, ImageCount(),
                       g_state.decoderName.c_str());
            text = extra;
            if (g_state.currentRotation) {
                wchar_t rot[24];
                swprintf_s(rot, L"   %d°", g_state.currentRotation);
                text += rot;
            }
        } else {
            text = L"Arrastra una imagen  ·  Ctrl+O abrir  ·  F11 pantalla completa";
        }
        if (!g_state.statusMessage.empty() &&
            (g_state.osdPinned || GetTickCount() - g_state.osdDisplayTime < OSD_MS)) {
            text += L"   ·   ";
            text += g_state.statusMessage;
        }
        DrawTextW(hdc, text.c_str(), -1, &info, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    SelectObject(hdc, hOld);
    DeleteObject(hFont);
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

void RenderEmptyState(HDC hdc, const RECT& clientRect) {
    RECT view = clientRect;
    view.bottom = std::max(view.top, view.bottom - HUD_HEIGHT);
    HBRUSH hBrush = CreateSolidBrush(BG_COLOR);
    FillRect(hdc, &view, hBrush);
    DeleteObject(hBrush);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(220, 220, 220));
    HFONT hTitle = CreateFontW(26, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT hHint = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT old = static_cast<HFONT>(SelectObject(hdc, hTitle));
    RECT title = view;
    title.bottom = view.top + (view.bottom - view.top) / 2 + 10;
    DrawTextW(hdc, L"ARTPICST", -1, &title, DT_SINGLELINE | DT_CENTER | DT_BOTTOM);
    SelectObject(hdc, hHint);
    SetTextColor(hdc, RGB(160, 160, 165));
    RECT hint = view;
    hint.top = title.bottom + 8;
    DrawTextW(hdc, L"Arrastra una imagen o carpeta  ·  Ctrl+O abrir archivo  ·  Ctrl+Shift+O carpeta",
              -1, &hint, DT_SINGLELINE | DT_CENTER | DT_TOP);
    SelectObject(hdc, old);
    DeleteObject(hTitle);
    DeleteObject(hHint);
}

void RenderImage() {
    if (!g_state.hdcMem || !g_state.hwnd) return;
    RECT rect{};
    GetClientRect(g_state.hwnd, &rect);
    HBRUSH hBrush = CreateSolidBrush(BG_COLOR);
    FillRect(g_state.hdcMem, &rect, hBrush);
    DeleteObject(hBrush);

    if (!g_state.imageData) {
        RenderEmptyState(g_state.hdcMem, rect);
        RenderHud(g_state.hdcMem, rect);
        return;
    }

    Bitmap bitmap(g_state.imageWidth, g_state.imageHeight,
                  g_state.imageWidth * 4, PixelFormat32bppARGB, g_state.imageData);
    Graphics graphics(g_state.hdcMem);
    graphics.SetClip(Rect(0, 0, rect.right, ViewHeight(rect.bottom)));
    graphics.SetInterpolationMode(g_state.zoom >= 1.0f
                                      ? InterpolationModeNearestNeighbor
                                      : InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(PixelOffsetModeHalf);
    graphics.SetSmoothingMode(SmoothingModeNone);

    int boxW = 0, boxH = 0;
    DisplaySize(boxW, boxH);
    const float destW = boxW * g_state.zoom;
    const float destH = boxH * g_state.zoom;
    const float centerX = g_state.offsetX + destW * 0.5f;
    const float centerY = g_state.offsetY + destH * 0.5f;

    GraphicsState state = graphics.Save();
    graphics.TranslateTransform(centerX, centerY);
    graphics.RotateTransform(static_cast<REAL>(g_state.currentRotation));
    graphics.TranslateTransform(-g_state.imageWidth * g_state.zoom * 0.5f,
                                -g_state.imageHeight * g_state.zoom * 0.5f);
    graphics.DrawImage(&bitmap, 0.0f, 0.0f,
                       g_state.imageWidth * g_state.zoom,
                       g_state.imageHeight * g_state.zoom);
    graphics.Restore(state);
    RenderHud(g_state.hdcMem, rect);
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
    if (g_state.currentRotation != 0) {
        if (rotated.GetLastStatus() != Ok) return false;
        Graphics g(&rotated);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.TranslateTransform(static_cast<REAL>(boxW) * 0.5f, static_cast<REAL>(boxH) * 0.5f);
        g.RotateTransform(static_cast<REAL>(g_state.currentRotation));
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
    ShowOSD(L"Imagen copiada");
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
    ShowOSD(g_state.isFullscreen ? L"Pantalla completa" : L"Ventana");
    InvalidateRect(g_state.hwnd, nullptr, FALSE);
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

bool OpenImageFileDialog(HWND hwnd) {
    ComPtr<IFileOpenDialog> dialog;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog.p));
    if (FAILED(hr) || !dialog) return false;
    COMDLG_FILTERSPEC filters[] = {
        { L"Imágenes", L"*.jpg;*.jpeg;*.jpe;*.jfif;*.png;*.bmp;*.gif;*.tif;*.tiff;*.webp;*.heic;*.heif;*.avif;*.jxl;*.jxr;*.wdp;*.ico;*.cur;*.tga;*.psd;*.hdr;*.jp2;*.dng;*.cr2;*.nef;*.arw;*.raw;*.emf;*.wmf" },
        { L"Todos los archivos", L"*.*" }
    };
    dialog->SetFileTypes(2, filters);
    dialog->SetTitle(L"Abrir imagen");
    FILEOPENDIALOGOPTIONS opts = 0;
    dialog->GetOptions(&opts);
    dialog->SetOptions(opts | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM);
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
                ShowOSD(L"Ajuste");
                InvalidateRect(g_state.hwnd, nullptr, FALSE);
            }
            break;
        case HUD_ONE: ActualSize(); break;
        case HUD_ROT: RotateImage(); break;
        case HUD_FULL: ToggleFullscreen(); break;
        case HUD_OPEN: OpenImageFileDialog(g_state.hwnd); break;
        default: break;
    }
}

void ShowContextMenu(HWND hwnd, int x, int y) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"Abrir imagen\tCtrl+O");
    AppendMenuW(menu, MF_STRING, 2, L"Abrir carpeta\tCtrl+Shift+O");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 3, L"Copiar imagen\tCtrl+C");
    AppendMenuW(menu, MF_STRING, 4, L"Copiar ruta\tCtrl+Shift+C");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 5, L"Ajustar a ventana\tF");
    AppendMenuW(menu, MF_STRING, 6, L"Tamaño real\t1");
    AppendMenuW(menu, MF_STRING, 9, L"Acercar\t+");
    AppendMenuW(menu, MF_STRING, 10, L"Alejar\t-");
    AppendMenuW(menu, MF_STRING, 7, L"Rotar\tR");
    AppendMenuW(menu, MF_STRING, 8, L"Pantalla completa\tF11");
    const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, x, y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    switch (cmd) {
        case 1: OpenImageFileDialog(hwnd); break;
        case 2: OpenFolderDialog(hwnd); break;
        case 3: CopyImageToClipboard(); break;
        case 4: CopyPathToClipboard(); break;
        case 5: InvokeHud(HUD_FIT); break;
        case 6: ActualSize(); break;
        case 7: RotateImage(); break;
        case 8: ToggleFullscreen(); break;
        case 9: {
            RECT client{};
            GetClientRect(hwnd, &client);
            ZoomAt(ZOOM_STEP, (client.right - client.left) / 2, ViewHeight(client.bottom) / 2);
            break;
        }
        case 10: {
            RECT client{};
            GetClientRect(hwnd, &client);
            ZoomAt(1.0f / ZOOM_STEP, (client.right - client.left) / 2, ViewHeight(client.bottom) / 2);
            break;
        }
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
                case VK_UP:
                case VK_BACK:
                    PreviousImage();
                    break;
                case VK_RIGHT:
                case VK_DOWN:
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
                    else PostMessageW(hwnd, WM_CLOSE, 0, 0);
                    break;
                case VK_F11:
                    ToggleFullscreen();
                    break;
                case VK_F1:
                    ShowOSD(L"Flechas navegar · rueda zoom · F ajustar · R rotar · F11 pantalla");
                    InvalidateRect(hwnd, nullptr, FALSE);
                    break;
                case VK_OEM_PLUS:
                case VK_ADD: {
                    RECT client{};
                    GetClientRect(hwnd, &client);
                    ZoomAt(ZOOM_STEP, (client.right - client.left) / 2, ViewHeight(client.bottom) / 2);
                    break;
                }
                case VK_OEM_MINUS:
                case VK_SUBTRACT: {
                    RECT client{};
                    GetClientRect(hwnd, &client);
                    ZoomAt(1.0f / ZOOM_STEP, (client.right - client.left) / 2, ViewHeight(client.bottom) / 2);
                    break;
                }
                case 'I':
                    g_state.osdPinned = !g_state.osdPinned;
                    ShowOSD(g_state.osdPinned ? L"Info fija" : L"Info auto");
                    InvalidateRect(hwnd, nullptr, FALSE);
                    break;
                case 'F':
                    InvokeHud(HUD_FIT);
                    break;
                case 'R':
                    RotateImage();
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
            RECT client{};
            GetClientRect(hwnd, &client);
            if (pt.y >= client.bottom - HUD_HEIGHT) return 0;
            ZoomAt((delta > 0) ? ZOOM_STEP : (1.0f / ZOOM_STEP), pt.x, pt.y);
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            RECT client{};
            GetClientRect(hwnd, &client);
            if (pt.y < client.bottom - HUD_HEIGHT) ToggleFullscreen();
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
            RECT client{};
            GetClientRect(hwnd, &client);
            if (y >= client.bottom - HUD_HEIGHT) return 0;
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
            const HudId hot = HitTestHud(x, y);
            if (hot != g_state.hudHot) {
                g_state.hudHot = hot;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            if (g_state.isDragging) {
                g_state.offsetX = g_state.dragStartOffsetX + static_cast<float>(x - g_state.dragStartX);
                g_state.offsetY = g_state.dragStartOffsetY + static_cast<float>(y - g_state.dragStartY);
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
                InvalidateRect(hwnd, nullptr, FALSE);
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
            StopPrefetchThread();
            FreeCurrentImage();
            FreeDoubleBuffer();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

std::wstring GetExecutableFolder() {
    wchar_t path[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return {};
    std::wstring full(path);
    size_t pos = full.find_last_of(L'\\');
    if (pos == std::wstring::npos) return {};
    return full.substr(0, pos);
}

std::wstring GetDefaultImageFolder() {
    wchar_t pictures[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_MYPICTURES, nullptr, SHGFP_TYPE_CURRENT, pictures)) && pictures[0]) {
        DWORD attrs = GetFileAttributesW(pictures);
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) return pictures;
    }
    std::wstring exeFolder = GetExecutableFolder();
    if (!exeFolder.empty()) return exeFolder;
    return L".";
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
        MessageBoxW(nullptr, L"Error al inicializar GDI+.", L"ARTPICST", MB_OK | MB_ICONERROR);
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
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;

    if (!RegisterClassExW(&wc)) {
        MessageBoxW(nullptr, L"Error al registrar la ventana.", L"ARTPICST", MB_OK | MB_ICONERROR);
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
        MessageBoxW(nullptr, L"Error al crear la ventana.", L"ARTPICST", MB_OK | MB_ICONERROR);
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
