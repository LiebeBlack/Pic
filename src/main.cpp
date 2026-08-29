#define STB_IMAGE_IMPLEMENTATION
#include "../include/stb_image.h"
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <VersionHelpers.h>
#include <shlobj.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <map>
#include <list>
#include <chrono>
#include <atomic>
#include <cwctype>
#include <cstdio>
#include <fstream>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "ole32.lib")

using namespace Gdiplus;

// Configuración de la ventana
const wchar_t CLASS_NAME[] = L"ARTPICSTWindow";
const int BG_COLOR = 0x121212; // #121212
const float MIN_ZOOM = 0.05f;
const float MAX_ZOOM = 100.0f;
const float ZOOM_STEP = 1.15f;
const size_t CACHE_SIZE = 5; // Caché LRU de 5 imágenes

// Estructura para datos de imagen en caché
struct CachedImage {
    unsigned char* data;
    int width;
    int height;
    int channels;
    std::wstring filepath;
    std::chrono::steady_clock::time_point lastAccess;
    int rotation; // 0, 90, 180, 270 grados
    
    CachedImage() : data(nullptr), width(0), height(0), channels(0), rotation(0) {}
    
    ~CachedImage() {
        if (data) {
            stbi_image_free(data);
            data = nullptr;
        }
    }
    
    // Prohibir copia para evitar duplicación de punteros
    CachedImage(const CachedImage&) = delete;
    CachedImage& operator=(const CachedImage&) = delete;
    
    // Permitir movimiento
    CachedImage(CachedImage&& other) noexcept 
        : data(other.data), width(other.width), height(other.height), 
          channels(other.channels), filepath(std::move(other.filepath)),
          lastAccess(other.lastAccess), rotation(other.rotation) {
        other.data = nullptr;
        other.width = 0;
        other.height = 0;
        other.channels = 0;
    }
    
    CachedImage& operator=(CachedImage&& other) noexcept {
        if (this != &other) {
            if (data) stbi_image_free(data);
            data = other.data;
            width = other.width;
            height = other.height;
            channels = other.channels;
            filepath = std::move(other.filepath);
            lastAccess = other.lastAccess;
            rotation = other.rotation;
            other.data = nullptr;
            other.width = 0;
            other.height = 0;
            other.channels = 0;
        }
        return *this;
    }
};

enum class WindowMode {
    Normal,
    Maximized,
    Fullscreen
};

// Estado de la aplicación
struct AppState {
    HWND hwnd;
    HDC hdcMem;
    HBITMAP hbmMem;
    HBITMAP hbmOld;
    
    // Datos de imagen actual
    unsigned char* imageData;
    int imageWidth;
    int imageHeight;
    int imageChannels;
    int currentRotation; // 0, 90, 180, 270 grados
    
    // Navegación
    std::vector<std::wstring> imageFiles;
    size_t currentImageIndex;
    std::wstring currentFolder;
    
    // Transformación
    float zoom;
    float offsetX;
    float offsetY;
    
    // Estado del ratón
    bool isDragging;
    int dragStartX;
    int dragStartY;
    float dragStartOffsetX;
    float dragStartOffsetY;
    
    // Caché LRU
    std::map<std::wstring, std::list<CachedImage>::iterator> cacheIndex;
    std::list<CachedImage> imageCache;
    std::mutex cacheMutex;
    
    // Pre-carga en segundo plano
    std::thread prefetchThread;
    std::atomic<bool> prefetchRunning;
    std::atomic<bool> prefetchRequested;
    std::condition_variable prefetchCV;
    std::mutex prefetchMutex;
    size_t prefetchTargetIndex;
    
    // OSD (On-Screen Display)
    bool showOSD;
    DWORD osdDisplayTime;
    
    // Fullscreen / ventana
    bool isFullscreen;
    WindowMode windowMode;
    bool hasPreviousWindowRect;
    RECT previousWindowRect;
    
    // Manejo de errores
    std::wstring lastError;
    int errorCount;
    
    // GDI+
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    
    AppState() : hwnd(nullptr), hdcMem(nullptr), hbmMem(nullptr), hbmOld(nullptr),
                 imageData(nullptr), imageWidth(0), imageHeight(0), imageChannels(0), currentRotation(0),
                 currentImageIndex(0), zoom(1.0f), offsetX(0.0f), offsetY(0.0f),
                 isDragging(false), dragStartX(0), dragStartY(0), 
                 dragStartOffsetX(0.0f), dragStartOffsetY(0.0f),
                 prefetchRunning(false), prefetchRequested(false), prefetchTargetIndex(0),
                 showOSD(true), osdDisplayTime(0), isFullscreen(false), windowMode(WindowMode::Maximized),
                 hasPreviousWindowRect(false), previousWindowRect{0,0,0,0}, errorCount(0), gdiplusToken(0) {
        gdiplusStartupInput.GdiplusVersion = 1;
    }
    
    ~AppState() {
        // Limpieza automática en caso de destrucción
        if (imageData) {
            stbi_image_free(imageData);
            imageData = nullptr;
        }
    }
};

AppState g_state;

// Declaraciones adelantadas para evitar errores de compilación por uso antes de definición
std::wstring GetFileName(const std::wstring& filepath);
std::wstring GetFileSizeString(const std::wstring& filepath);
void FreeCurrentImage();
void ScanFolderForImages(const std::wstring& folderPath);
bool LoadImage(const std::wstring& filepath);
bool LoadImageByIndex(size_t index);
void RequestPrefetch(size_t targetIndex);
void StartPrefetchThread();
void RotateImage();
void ToggleFullscreen();
bool CopyPathToClipboard();
bool CopyImageToClipboard();
void FitImageToWindow(int windowWidth, int windowHeight);

// Inicializar GDI+
bool InitGDIPlus() {
    return GdiplusStartup(&g_state.gdiplusToken, &g_state.gdiplusStartupInput, NULL) == Ok;
}

// Limpiar GDI+
void CleanupGDIPlus() {
    // Detener hilo de pre-carga de forma segura
    g_state.prefetchRunning = false;
    g_state.prefetchRequested = true;
    g_state.prefetchCV.notify_all();
    
    // Esperar al hilo con timeout para evitar deadlock
    if (g_state.prefetchThread.joinable()) {
        auto start = std::chrono::steady_clock::now();
        while (g_state.prefetchThread.joinable()) {
            if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) {
                // Timeout: forzar terminación
                g_state.prefetchThread.detach();
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (g_state.prefetchThread.joinable()) {
                g_state.prefetchThread.join();
                break;
            }
        }
    }
    
    // Limpiar caché de forma segura
    {
        std::lock_guard<std::mutex> lock(g_state.cacheMutex);
        g_state.imageCache.clear();
        g_state.cacheIndex.clear();
    }
    
    // Liberar imagen actual
    FreeCurrentImage();
    
    // Liberar recursos GDI+
    if (g_state.hdcMem) {
        SelectObject(g_state.hdcMem, g_state.hbmOld);
        DeleteObject(g_state.hbmMem);
        DeleteDC(g_state.hdcMem);
        g_state.hdcMem = nullptr;
        g_state.hbmMem = nullptr;
        g_state.hbmOld = nullptr;
    }
    
    if (g_state.gdiplusToken) {
        GdiplusShutdown(g_state.gdiplusToken);
        g_state.gdiplusToken = 0;
    }
}

// Obtener imagen de caché con sincronización mejorada
CachedImage* GetFromCache(const std::wstring& filepath) {
    std::lock_guard<std::mutex> lock(g_state.cacheMutex);
    
    try {
        auto it = g_state.cacheIndex.find(filepath);
        if (it != g_state.cacheIndex.end()) {
            // Validar que el iterador sigue siendo válido
            if (it->second == g_state.imageCache.end()) {
                g_state.cacheIndex.erase(it);
                return nullptr;
            }
            
            // Mover al final (más reciente) de forma segura
            g_state.imageCache.splice(g_state.imageCache.end(), g_state.imageCache, it->second);
            it->second = std::prev(g_state.imageCache.end());
            it->second->lastAccess = std::chrono::steady_clock::now();
            
            return &(*it->second);
        }
    } catch (...) {
        // Si hay algún error en la caché, limpiarla y retornar nullptr
        g_state.imageCache.clear();
        g_state.cacheIndex.clear();
    }
    
    return nullptr;
}

// Agregar imagen a caché con sincronización mejorada
void AddToCache(const std::wstring& filepath, unsigned char* data, int width, int height, int channels) {
    if (!data || width <= 0 || height <= 0 || channels <= 0) {
        if (data) stbi_image_free(data);
        return;
    }
    
    std::lock_guard<std::mutex> lock(g_state.cacheMutex);
    
    try {
        // Si ya existe, actualizar de forma segura
        auto it = g_state.cacheIndex.find(filepath);
        if (it != g_state.cacheIndex.end()) {
            if (it->second != g_state.imageCache.end()) {
                g_state.imageCache.erase(it->second);
            }
            g_state.cacheIndex.erase(it);
        }
        
        // Crear entrada de caché
        CachedImage cached;
        cached.data = data;
        cached.width = width;
        cached.height = height;
        cached.channels = channels;
        cached.filepath = filepath;
        cached.lastAccess = std::chrono::steady_clock::now();
        cached.rotation = 0;
        
        g_state.imageCache.push_back(std::move(cached));
        g_state.cacheIndex[filepath] = std::prev(g_state.imageCache.end());
        
        // Aplicar LRU si excede el tamaño con validación
        while (g_state.imageCache.size() > CACHE_SIZE) {
            if (g_state.imageCache.empty()) break;
            
            auto oldest = g_state.imageCache.begin();
            auto indexIt = g_state.cacheIndex.find(oldest->filepath);
            if (indexIt != g_state.cacheIndex.end()) {
                g_state.cacheIndex.erase(indexIt);
            }
            g_state.imageCache.pop_front();
        }
    } catch (...) {
        // Si falla, liberar memoria y limpiar caché corrupta
        if (data) stbi_image_free(data);
        g_state.imageCache.clear();
        g_state.cacheIndex.clear();
    }
}

// Limpiar caché
void ClearCache() {
    std::lock_guard<std::mutex> lock(g_state.cacheMutex);
    g_state.imageCache.clear();
    g_state.cacheIndex.clear();
}

// Liberar imagen actual de forma segura
void FreeCurrentImage() {
    if (g_state.imageData) {
        stbi_image_free(g_state.imageData);
        g_state.imageData = nullptr;
    }
    g_state.imageWidth = 0;
    g_state.imageHeight = 0;
    g_state.imageChannels = 0;
    g_state.currentRotation = 0;
}

// Validar integridad de archivo
bool ValidateFileIntegrity(const std::wstring& filepath) {
    WIN32_FILE_ATTRIBUTE_DATA fileData;
    if (!GetFileAttributesEx(filepath.c_str(), GetFileExInfoStandard, &fileData)) {
        return false;
    }
    
    // Verificar que no sea un directorio
    if (fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        return false;
    }
    
    // Verificar tamaño razonable (máximo 500MB para evitar DOS)
    LARGE_INTEGER size;
    size.HighPart = fileData.nFileSizeHigh;
    size.LowPart = fileData.nFileSizeLow;
    
    if (size.QuadPart > 500LL * 1024LL * 1024LL) { // 500MB
        return false;
    }
    
    // Verificar que el archivo sea accesible
    HANDLE hFile = CreateFile(filepath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }
    CloseHandle(hFile);
    
    return true;
}

// Registro simple de diagnóstico para ver qué ocurre al arrancar o cerrar la app.
void LogMessage(const std::wstring& message) {
    try {
        std::wofstream logFile(L"artpicst.log", std::ios::app);
        if (logFile.is_open()) {
            logFile << L"[" << std::chrono::system_clock::now().time_since_epoch().count() << L"] " << message << L"\n";
            logFile.close();
        }
    } catch (...) {
        // Silencio intencional: no romper la app al escribir log.
    }
}

// Manejo de errores robusto con graceful degradation
void HandleError(const std::wstring& errorMsg, bool showUser = true) {
    g_state.lastError = errorMsg;
    g_state.errorCount++;
    LogMessage(errorMsg);
    
    // Logging de errores para depuración
    #ifdef _DEBUG
    {
        // En modo debug, escribir a archivo de log
        static FILE* logFile = nullptr;
        if (!logFile) {
            logFile = fopen("artpicst_debug.log", "a");
        }
        if (logFile) {
            int size_needed = WideCharToMultiByte(CP_UTF8, 0, errorMsg.c_str(), -1, NULL, 0, NULL, NULL);
            if (size_needed > 0) {
                std::string errorMsgUtf8(size_needed, 0);
                WideCharToMultiByte(CP_UTF8, 0, errorMsg.c_str(), -1, &errorMsgUtf8[0], size_needed, NULL, NULL);
                fprintf(logFile, "[ERROR] %s\n", errorMsgUtf8.c_str());
                fflush(logFile);
            }
        }
    }
    #endif
    
    // Limitar frecuencia de mensajes de error al usuario
    static DWORD lastErrorTime = 0;
    DWORD currentTime = GetTickCount();
    
    if (showUser && (currentTime - lastErrorTime > 3000 || g_state.errorCount <= 3)) {
        // Solo mostrar errores críticos al usuario
        if (g_state.errorCount > 5) {
            // Graceful degradation: demasiados errores, mostrar mensaje una sola vez
            MessageBox(g_state.hwnd, 
                      L"Se han producido varios errores. La aplicación continuará funcionando con funcionalidad reducida.",
                      L"ARTPICST - Modo Degradado", MB_OK | MB_ICONINFORMATION);
            g_state.errorCount = 0; // Resetear para evitar spam
        } else {
            MessageBox(g_state.hwnd, errorMsg.c_str(), L"ARTPICST Error", MB_OK | MB_ICONWARNING);
        }
        lastErrorTime = currentTime;
    }
    
    // Graceful degradation: Si hay demasiados errores, reducir funcionalidad
    if (g_state.errorCount > 8) {
        // Desactivar pre-carga para reducir carga
        g_state.prefetchRunning = false;
        
        // Reducir tamaño de caché
        const size_t REDUCED_CACHE_SIZE = 2;
        std::lock_guard<std::mutex> lock(g_state.cacheMutex);
        while (g_state.imageCache.size() > REDUCED_CACHE_SIZE) {
            auto oldest = g_state.imageCache.begin();
            g_state.cacheIndex.erase(oldest->filepath);
            g_state.imageCache.pop_front();
        }
    }
    
    // Resetear contador después de muchos errores para recuperación
    if (g_state.errorCount > 15) {
        g_state.errorCount = 0;
        // Reactivar funcionalidad gradualmente
        g_state.prefetchRunning = true;
    }
}

// Comprueba si la extensión es compatible con los formatos que stb puede decodificar.
bool IsSupportedImageExtension(const std::wstring& ext) {
    static const std::wstring supported[] = {
        L"jpg", L"jpeg", L"jpe", L"jfif",
        L"png",
        L"bmp",
        L"gif",
        L"webp",
        L"tga",
        L"tif", L"tiff",
        L"ico", L"cur",
        L"psd",
        L"hdr",
        L"pic",
        L"pnm", L"ppm", L"pgm",
        L"avif", L"heic", L"heif",
        L"jxl", L"jxr"
    };

    for (const auto& item : supported) {
        if (ext == item) {
            return true;
        }
    }
    return false;
}

// Intenta validar si un archivo realmente es una imagen decodificable con stb.
bool CanDecodeImageFile(const std::wstring& filePath) {
    int w = 0, h = 0, c = 0;
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, filePath.c_str(), -1, NULL, 0, NULL, NULL);
    if (size_needed <= 0) {
        return false;
    }

    std::string utf8Path(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, filePath.c_str(), -1, &utf8Path[0], size_needed, NULL, NULL);

    unsigned char* probe = stbi_load(utf8Path.c_str(), &w, &h, &c, 4);
    if (probe) {
        stbi_image_free(probe);
        return w > 0 && h > 0;
    }

    return false;
}

// Obtener archivos de imagen en una carpeta
void ScanFolderForImages(const std::wstring& folderPath) {
    g_state.imageFiles.clear();
    g_state.currentFolder = folderPath;
    ClearCache();
    
    WIN32_FIND_DATA findData;
    std::wstring searchPath = folderPath + L"\\*";
    HANDLE hFind = FindFirstFile(searchPath.c_str(), &findData);
    
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                std::wstring filename = findData.cFileName;
                const size_t dotPos = filename.find_last_of(L'.');
                std::wstring ext;
                if (dotPos != std::wstring::npos) {
                    ext = filename.substr(dotPos + 1);
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                }

                if (!ext.empty() && IsSupportedImageExtension(ext)) {
                    g_state.imageFiles.push_back(folderPath + L"\\" + filename);
                    continue;
                }

                // Si tiene extensión rara o no tiene extensión, intentamos validar con stb.
                if (ext.empty() || !ext.empty()) {
                    const std::wstring fullPath = folderPath + L"\\" + filename;
                    if (CanDecodeImageFile(fullPath)) {
                        g_state.imageFiles.push_back(fullPath);
                    }
                }
            }
        } while (FindNextFile(hFind, &findData));
        FindClose(hFind);
    }
    
    // Ordenar archivos alfabéticamente
    std::sort(g_state.imageFiles.begin(), g_state.imageFiles.end());
}

// Cargar imagen con manejo robusto de errores
bool LoadImage(const std::wstring& filepath) {
    // Validar integridad del archivo primero
    if (!ValidateFileIntegrity(filepath)) {
        HandleError(L"Archivo inválido o inaccesible: " + GetFileName(filepath));
        return false;
    }
    
    FreeCurrentImage();
    
    try {
        // Verificar caché primero con try-catch para seguridad
        CachedImage* cached = nullptr;
        try {
            cached = GetFromCache(filepath);
        } catch (...) {
            // Si falla la caché, continuar con carga desde disco
            cached = nullptr;
        }
        
        if (cached) {
            // Copiar datos de caché con validación
            size_t dataSize = cached->width * cached->height * 4;
            if (dataSize == 0 || cached->width <= 0 || cached->height <= 0) {
                HandleError(L"Datos de caché corruptos: " + GetFileName(filepath));
                return false;
            }
            
            g_state.imageData = (unsigned char*)malloc(dataSize);
            if (!g_state.imageData) {
                HandleError(L"Error de memoria al cargar desde caché");
                return false;
            }
            
            memcpy(g_state.imageData, cached->data, dataSize);
            g_state.imageWidth = cached->width;
            g_state.imageHeight = cached->height;
            g_state.imageChannels = cached->channels;
            g_state.currentRotation = cached->rotation;
            ConvertRGBAtoBGRA(g_state.imageData, g_state.imageWidth, g_state.imageHeight);
        } else {
            // Cargar desde disco con manejo de errores
            int size_needed = WideCharToMultiByte(CP_UTF8, 0, filepath.c_str(), -1, NULL, 0, NULL, NULL);
            if (size_needed <= 0) {
                HandleError(L"Error al convertir ruta de archivo");
                return false;
            }
            
            std::string utf8Path(size_needed, 0);
            WideCharToMultiByte(CP_UTF8, 0, filepath.c_str(), -1, &utf8Path[0], size_needed, NULL, NULL);
            
            // Cargar con stb_image
            g_state.imageData = stbi_load(utf8Path.c_str(), &g_state.imageWidth, &g_state.imageHeight, &g_state.imageChannels, 4);
            
            if (!g_state.imageData) {
                const char* stbError = stbi_failure_reason();
                std::wstring errorMsg = L"Error al cargar imagen: " + GetFileName(filepath);
                if (stbError) {
                    int errSize_needed = MultiByteToWideChar(CP_UTF8, 0, stbError, -1, NULL, 0);
                    if (errSize_needed > 0) {
                        std::wstring stbErrorWide(errSize_needed, 0);
                        MultiByteToWideChar(CP_UTF8, 0, stbError, -1, &stbErrorWide[0], errSize_needed);
                        errorMsg += L"\nDetalles: " + stbErrorWide;
                    }
                }
                HandleError(errorMsg);
                return false;
            }

            ConvertRGBAtoBGRA(g_state.imageData, g_state.imageWidth, g_state.imageHeight);
            
            // Validar dimensiones
            if (g_state.imageWidth <= 0 || g_state.imageHeight <= 0 || g_state.imageChannels <= 0) {
                HandleError(L"Dimensiones de imagen inválidas: " + GetFileName(filepath));
                FreeCurrentImage();
                return false;
            }
            
            // Limitar dimensiones máximas para evitar DOS
            const int MAX_DIMENSION = 20000; // 20K pixels
            if (g_state.imageWidth > MAX_DIMENSION || g_state.imageHeight > MAX_DIMENSION) {
                HandleError(L"Imagen demasiado grande: " + GetFileName(filepath));
                FreeCurrentImage();
                return false;
            }
            
            // Agregar a caché de forma segura
            try {
                size_t dataSize = g_state.imageWidth * g_state.imageHeight * 4;
                unsigned char* cacheData = (unsigned char*)malloc(dataSize);
                if (cacheData) {
                    memcpy(cacheData, g_state.imageData, dataSize);
                    AddToCache(filepath, cacheData, g_state.imageWidth, g_state.imageHeight, g_state.imageChannels);
                }
            } catch (...) {
                // Si falla la caché, continuar sin ella
            }
        }
        
        // Resetear transformación
        g_state.zoom = 1.0f;
        g_state.offsetX = 0.0f;
        g_state.offsetY = 0.0f;

        if (g_state.hwnd) {
            RECT clientRect;
            GetClientRect(g_state.hwnd, &clientRect);
            FitImageToWindow(clientRect.right, clientRect.bottom);
            EnsureImageVisible();
        }
        
        // Mostrar OSD
        g_state.showOSD = true;
        g_state.osdDisplayTime = GetTickCount();
        
        return true;
        
    } catch (const std::exception& e) {
        std::string errorMsg = "Excepción al cargar imagen: " + std::string(e.what());
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, errorMsg.c_str(), -1, NULL, 0);
        if (size_needed > 0) {
            std::wstring errorMsgWide(size_needed, 0);
            MultiByteToWideChar(CP_UTF8, 0, errorMsg.c_str(), -1, &errorMsgWide[0], size_needed);
            HandleError(errorMsgWide);
        }
        FreeCurrentImage();
        return false;
    } catch (...) {
        HandleError(L"Error desconocido al cargar imagen: " + GetFileName(filepath));
        FreeCurrentImage();
        return false;
    }
}

// Cargar imagen por índice
bool LoadImageByIndex(size_t index) {
    if (index >= g_state.imageFiles.size()) {
        return false;
    }
    
    g_state.currentImageIndex = index;
    return LoadImage(g_state.imageFiles[index]);
}

// Solicitar pre-carga de imágenes
void RequestPrefetch(size_t targetIndex) {
    std::lock_guard<std::mutex> lock(g_state.prefetchMutex);
    g_state.prefetchTargetIndex = targetIndex;
    g_state.prefetchRequested = true;
    g_state.prefetchCV.notify_one();
}

// Hilo de pre-carga en segundo plano con sincronización robusta
void PrefetchThreadFunc() {
    while (g_state.prefetchRunning) {
        std::unique_lock<std::mutex> lock(g_state.prefetchMutex);
        
        // Esperar con timeout para evitar deadlock
        if (!g_state.prefetchCV.wait_for(lock, std::chrono::seconds(1), 
                                         []{ return g_state.prefetchRequested || !g_state.prefetchRunning; })) {
            continue; // Timeout, continuar para verificar running state
        }
        
        if (!g_state.prefetchRunning) break;
        
        g_state.prefetchRequested = false;
        size_t targetIndex = g_state.prefetchTargetIndex;
        lock.unlock();
        
        try {
            // Pre-cargar siguiente y anterior imagen con validación
            std::vector<size_t> indicesToPrefetch;
            
            // Acceso seguro a imageFiles
            {
                std::lock_guard<std::mutex> cacheLock(g_state.cacheMutex);
                if (!g_state.imageFiles.empty()) {
                    // Siguiente imagen
                    size_t nextIndex = (targetIndex + 1) % g_state.imageFiles.size();
                    indicesToPrefetch.push_back(nextIndex);
                    
                    // Imagen anterior
                    size_t prevIndex = (targetIndex == 0) ? g_state.imageFiles.size() - 1 : targetIndex - 1;
                    indicesToPrefetch.push_back(prevIndex);
                    
                    // Siguiente 2 imágenes
                    size_t next2Index = (targetIndex + 2) % g_state.imageFiles.size();
                    indicesToPrefetch.push_back(next2Index);
                    
                    // Anterior 2 imágenes
                    size_t prev2Index = (targetIndex == 0 || targetIndex == 1) ? 
                                       g_state.imageFiles.size() - (2 - targetIndex) : targetIndex - 2;
                    indicesToPrefetch.push_back(prev2Index);
                }
            }
            
            // Cargar imágenes en segundo plano con manejo de errores
            for (size_t idx : indicesToPrefetch) {
                if (!g_state.prefetchRunning) break;
                
                // Acceso seguro a imageFiles
                std::wstring filepath;
                {
                    std::lock_guard<std::mutex> cacheLock(g_state.cacheMutex);
                    if (idx < g_state.imageFiles.size()) {
                        filepath = g_state.imageFiles[idx];
                    } else {
                        continue;
                    }
                }
                
                // Verificar si ya está en caché
                if (GetFromCache(filepath) == nullptr) {
                    // Validar archivo antes de cargar
                    if (!ValidateFileIntegrity(filepath)) {
                        continue;
                    }
                    
                    try {
                        // Cargar en segundo plano
                        int size_needed = WideCharToMultiByte(CP_UTF8, 0, filepath.c_str(), -1, NULL, 0, NULL, NULL);
                        if (size_needed <= 0) continue;
                        
                        std::string utf8Path(size_needed, 0);
                        WideCharToMultiByte(CP_UTF8, 0, filepath.c_str(), -1, &utf8Path[0], size_needed, NULL, NULL);
                        
                        int width, height, channels;
                        unsigned char* data = stbi_load(utf8Path.c_str(), &width, &height, &channels, 4);
                        
                        if (data && width > 0 && height > 0 && channels > 0) {
                            // Validar dimensiones
                            const int MAX_DIMENSION = 20000;
                            if (width <= MAX_DIMENSION && height <= MAX_DIMENSION) {
                                size_t dataSize = width * height * 4;
                                unsigned char* cacheData = (unsigned char*)malloc(dataSize);
                                if (cacheData) {
                                    memcpy(cacheData, data, dataSize);
                                    AddToCache(filepath, cacheData, width, height, channels);
                                }
                            }
                            stbi_image_free(data);
                        }
                    } catch (...) {
                        // Ignorar errores en pre-carga, no afectar al hilo principal
                    }
                }
                
                // Pequeña pausa para no saturar CPU
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        } catch (...) {
            // Cualquier excepción en el hilo de pre-carga no debe terminar el programa
            continue;
        }
    }
}

// Iniciar hilo de pre-carga
void StartPrefetchThread() {
    g_state.prefetchRunning = true;
    g_state.prefetchThread = std::thread(PrefetchThreadFunc);
}

// Rotar imagen 90 grados
void RotateImage() {
    if (!g_state.imageData || g_state.imageWidth == 0 || g_state.imageHeight == 0) {
        return;
    }
    
    g_state.currentRotation = (g_state.currentRotation + 90) % 360;
    
    if (g_state.hwnd) {
        RECT clientRect;
        GetClientRect(g_state.hwnd, &clientRect);
        FitImageToWindow(clientRect.right, clientRect.bottom);
        EnsureImageVisible();
    }
    
    if (g_state.currentImageIndex < g_state.imageFiles.size()) {
        const std::wstring& filepath = g_state.imageFiles[g_state.currentImageIndex];
        std::lock_guard<std::mutex> lock(g_state.cacheMutex);
        auto it = g_state.cacheIndex.find(filepath);
        if (it != g_state.cacheIndex.end() && it->second != g_state.imageCache.end()) {
            it->second->rotation = g_state.currentRotation;
        }
    }
    
    InvalidateRect(g_state.hwnd, NULL, FALSE);
}

// Ajustar la imagen a la ventana y mantenerla centrada cuando cambia el tamaño
void UpdateViewportLayout(bool forceFit = false) {
    if (!g_state.hwnd || !g_state.imageData) {
        return;
    }

    RECT rect;
    GetClientRect(g_state.hwnd, &rect);
    if (rect.right <= 0 || rect.bottom <= 0) {
        return;
    }

    if (forceFit || g_state.zoom <= 0.0f) {
        FitImageToWindow(rect.right, rect.bottom);
    } else {
        EnsureImageVisible();
    }
}

// Alternar modo fullscreen
void ToggleFullscreen() {
    if (!g_state.hwnd) return;

    if (!g_state.isFullscreen) {
        if (!g_state.hasPreviousWindowRect) {
            GetWindowRect(g_state.hwnd, &g_state.previousWindowRect);
            g_state.hasPreviousWindowRect = true;
        }

        HMONITOR hMonitor = MonitorFromWindow(g_state.hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfo(hMonitor, &mi);

        SetWindowPos(g_state.hwnd, HWND_TOP,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        g_state.isFullscreen = true;
    } else {
        if (g_state.hasPreviousWindowRect) {
            SetWindowPos(g_state.hwnd, HWND_TOP,
                         g_state.previousWindowRect.left,
                         g_state.previousWindowRect.top,
                         g_state.previousWindowRect.right - g_state.previousWindowRect.left,
                         g_state.previousWindowRect.bottom - g_state.previousWindowRect.top,
                         SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        }
        g_state.isFullscreen = false;
    }

    UpdateViewportLayout(true);
    InvalidateRect(g_state.hwnd, NULL, FALSE);
}

// Copiar ruta al portapapeles
bool CopyPathToClipboard() {
    if (g_state.imageFiles.empty() || g_state.currentImageIndex >= g_state.imageFiles.size()) {
        return false;
    }
    
    const std::wstring& filepath = g_state.imageFiles[g_state.currentImageIndex];
    
    if (!OpenClipboard(g_state.hwnd)) {
        return false;
    }
    
    EmptyClipboard();
    
    // Asignar memoria para el texto
    size_t size = (filepath.length() + 1) * sizeof(wchar_t);
    HGLOBAL hClipboardData = GlobalAlloc(GMEM_MOVEABLE, size);
    if (hClipboardData) {
        wchar_t* pchData = (wchar_t*)GlobalLock(hClipboardData);
        if (pchData) {
            wcscpy_s(pchData, filepath.length() + 1, filepath.c_str());
            GlobalUnlock(hClipboardData);
            SetClipboardData(CF_UNICODETEXT, hClipboardData);
        }
    }
    
    CloseClipboard();
    
    // Mostrar confirmación en OSD
    g_state.showOSD = true;
    g_state.osdDisplayTime = GetTickCount();
    
    return true;
}

// Copiar imagen al portapapeles
bool CopyImageToClipboard() {
    if (!g_state.imageData || g_state.imageWidth == 0 || g_state.imageHeight == 0) {
        return false;
    }
    
    // Crear bitmap desde datos de imagen
    Bitmap* bitmap = new Bitmap(g_state.imageWidth, g_state.imageHeight, 
                                 g_state.imageWidth * 4, PixelFormat32bppARGB, 
                                 g_state.imageData);
    
    if (!bitmap) {
        return false;
    }
    
    // Convertir a HBITMAP para el portapapeles
    HBITMAP hBitmap = NULL;
    bitmap->GetHBITMAP(Color(0, 0, 0, 0), &hBitmap);
    delete bitmap;
    
    if (!hBitmap) {
        return false;
    }
    
    if (!OpenClipboard(g_state.hwnd)) {
        DeleteObject(hBitmap);
        return false;
    }
    
    EmptyClipboard();
    SetClipboardData(CF_BITMAP, hBitmap);
    CloseClipboard();
    
    // No borrar hBitmap, el portapapeles se encarga
    
    // Mostrar confirmación en OSD
    g_state.showOSD = true;
    g_state.osdDisplayTime = GetTickCount();
    
    return true;
}

// Siguiente imagen
void NextImage() {
    if (g_state.imageFiles.empty()) return;
    
    size_t nextIndex = (g_state.currentImageIndex + 1) % g_state.imageFiles.size();
    LoadImageByIndex(nextIndex);
    RequestPrefetch(nextIndex);
    InvalidateRect(g_state.hwnd, NULL, FALSE);
}

// Imagen anterior
void PreviousImage() {
    if (g_state.imageFiles.empty()) return;
    
    size_t prevIndex = (g_state.currentImageIndex == 0) ? g_state.imageFiles.size() - 1 : g_state.currentImageIndex - 1;
    LoadImageByIndex(prevIndex);
    RequestPrefetch(prevIndex);
    InvalidateRect(g_state.hwnd, NULL, FALSE);
}

// Normalizar los píxeles de RGBA de stb a BGRA para GDI+
void ConvertRGBAtoBGRA(unsigned char* pixels, int width, int height) {
    if (!pixels || width <= 0 || height <= 0) {
        return;
    }

    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    for (size_t i = 0; i < pixelCount; ++i) {
        unsigned char* p = pixels + (i * 4);
        std::swap(p[0], p[2]);
    }
}

// Mantener la imagen visible dentro de la ventana aunque se haga zoom o pan
void EnsureImageVisible() {
    if (!g_state.imageData || g_state.imageWidth <= 0 || g_state.imageHeight <= 0 || !g_state.hwnd) {
        return;
    }

    RECT clientRect;
    GetClientRect(g_state.hwnd, &clientRect);
    int windowWidth = clientRect.right - clientRect.left;
    int windowHeight = clientRect.bottom - clientRect.top;
    if (windowWidth <= 0 || windowHeight <= 0) {
        return;
    }

    int imageWidth = g_state.imageWidth;
    int imageHeight = g_state.imageHeight;
    if (g_state.currentRotation == 90 || g_state.currentRotation == 270) {
        std::swap(imageWidth, imageHeight);
    }

    float imageDrawWidth = imageWidth * g_state.zoom;
    float imageDrawHeight = imageHeight * g_state.zoom;

    if (imageDrawWidth < windowWidth) {
        g_state.offsetX = (windowWidth - imageDrawWidth) * 0.5f;
    } else {
        float maxOffsetX = 0.0f;
        float minOffsetX = windowWidth - imageDrawWidth;
        if (g_state.offsetX > maxOffsetX) {
            g_state.offsetX = maxOffsetX;
        }
        if (g_state.offsetX < minOffsetX) {
            g_state.offsetX = minOffsetX;
        }
    }

    if (imageDrawHeight < windowHeight) {
        g_state.offsetY = (windowHeight - imageDrawHeight) * 0.5f;
    } else {
        float maxOffsetY = 0.0f;
        float minOffsetY = windowHeight - imageDrawHeight;
        if (g_state.offsetY > maxOffsetY) {
            g_state.offsetY = maxOffsetY;
        }
        if (g_state.offsetY < minOffsetY) {
            g_state.offsetY = minOffsetY;
        }
    }
}

// Ajustar zoom para ajustar imagen a ventana y dejarla centrada
void FitImageToWindow(int windowWidth, int windowHeight) {
    if (!g_state.imageData || g_state.imageWidth == 0 || g_state.imageHeight == 0) {
        return;
    }
    if (windowWidth <= 0 || windowHeight <= 0) {
        return;
    }

    int imageWidth = g_state.imageWidth;
    int imageHeight = g_state.imageHeight;
    if (g_state.currentRotation == 90 || g_state.currentRotation == 270) {
        std::swap(imageWidth, imageHeight);
    }

    float scaleX = static_cast<float>(windowWidth) / static_cast<float>(imageWidth);
    float scaleY = static_cast<float>(windowHeight) / static_cast<float>(imageHeight);
    float fitZoom = std::min(scaleX, scaleY) * 0.92f;
    fitZoom = std::max(MIN_ZOOM, std::min(MAX_ZOOM, fitZoom));

    g_state.zoom = fitZoom;
    g_state.offsetX = (windowWidth - imageWidth * g_state.zoom) * 0.5f;
    g_state.offsetY = (windowHeight - imageHeight * g_state.zoom) * 0.5f;
    EnsureImageVisible();
}

// Obtener tamaño de archivo en formato legible
std::wstring GetFileSizeString(const std::wstring& filepath) {
    WIN32_FILE_ATTRIBUTE_DATA fileData;
    if (GetFileAttributesEx(filepath.c_str(), GetFileExInfoStandard, &fileData)) {
        LARGE_INTEGER size;
        size.HighPart = fileData.nFileSizeHigh;
        size.LowPart = fileData.nFileSizeLow;
        
        const wchar_t* units[] = { L"B", L"KB", L"MB", L"GB" };
        int unitIndex = 0;
        double fileSize = (double)size.QuadPart;
        
        while (fileSize >= 1024.0 && unitIndex < 3) {
            fileSize /= 1024.0;
            unitIndex++;
        }
        
        wchar_t buffer[32];
        swprintf_s(buffer, L"%.1f %s", fileSize, units[unitIndex]);
        return std::wstring(buffer);
    }
    return L"Unknown";
}

// Obtener nombre de archivo sin ruta
std::wstring GetFileName(const std::wstring& filepath) {
    if (filepath.empty()) {
        return L"";
    }

    size_t pos = filepath.find_last_of(L'\\');
    if (pos != std::wstring::npos) {
        return filepath.substr(pos + 1);
    }
    return filepath;
}

// Renderizar OSD (On-Screen Display) con información mejorada
void RenderOSD(HDC hdc) {
    if (!hdc || !g_state.hwnd || !g_state.showOSD || g_state.imageFiles.empty() ||
        g_state.currentImageIndex >= g_state.imageFiles.size()) {
        return;
    }

    if (GetTickCount() - g_state.osdDisplayTime > 3000) {
        g_state.showOSD = false;
        return;
    }

    RECT clientRect;
    GetClientRect(g_state.hwnd, &clientRect);
    if (clientRect.right <= clientRect.left || clientRect.bottom <= clientRect.top) {
        return;
    }

    HFONT hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (!hFont) {
        return;
    }

    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(200, 200, 200));

    const std::wstring& currentFile = g_state.imageFiles[g_state.currentImageIndex];
    std::wstring filename = GetFileName(currentFile);
    std::wstring fileSize = GetFileSizeString(currentFile);

    int displayWidth = g_state.imageWidth;
    int displayHeight = g_state.imageHeight;
    if (g_state.currentRotation == 90 || g_state.currentRotation == 270) {
        std::swap(displayWidth, displayHeight);
    }

    wchar_t rotationText[16] = L"";
    if (g_state.currentRotation > 0) {
        swprintf_s(rotationText, L" | %d°", g_state.currentRotation);
    }

    wchar_t zoomText[16];
    swprintf_s(zoomText, L" | %.0f%%", g_state.zoom * 100);

    wchar_t infoText[512];
    swprintf_s(infoText, L"%s | %dx%d%s | %s%s | %zu/%zu",
               filename.c_str(), displayWidth, displayHeight, rotationText,
               fileSize.c_str(), zoomText, g_state.currentImageIndex + 1, g_state.imageFiles.size());

    RECT osdRect = {10, clientRect.bottom - 40, clientRect.right - 10, clientRect.bottom - 10};
    if (osdRect.right <= osdRect.left || osdRect.bottom <= osdRect.top) {
        SelectObject(hdc, hOldFont);
        DeleteObject(hFont);
        return;
    }

    HDC hdcLayer = CreateCompatibleDC(hdc);
    HBITMAP hbmLayer = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
    if (hdcLayer && hbmLayer) {
        HBITMAP hbmOldLayer = (HBITMAP)SelectObject(hdcLayer, hbmLayer);
        if (hbmOldLayer) {
            BitBlt(hdcLayer, 0, 0, clientRect.right, clientRect.bottom, hdc, 0, 0, SRCCOPY);
            BLENDFUNCTION blend = {AC_SRC_OVER, 0, 128, 0};
            HBRUSH hBrush = CreateSolidBrush(RGB(18, 18, 18));
            if (hBrush) {
                FillRect(hdcLayer, &osdRect, hBrush);
                DeleteObject(hBrush);
            }
            AlphaBlend(hdc, osdRect.left, osdRect.top,
                       osdRect.right - osdRect.left, osdRect.bottom - osdRect.top,
                       hdcLayer, osdRect.left, osdRect.top,
                       osdRect.right - osdRect.left, osdRect.bottom - osdRect.top, blend);
            DrawText(hdc, infoText, -1, &osdRect, DT_BOTTOM | DT_LEFT | DT_SINGLELINE);
            SelectObject(hdcLayer, hbmOldLayer);
        }
        DeleteObject(hbmLayer);
        DeleteDC(hdcLayer);
    }

    SelectObject(hdc, hOldFont);
    DeleteObject(hFont);
}

// Crear buffer de doble búfer optimizado con graceful degradation
void CreateDoubleBuffer(int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    
    // Liberar recursos existentes de forma segura
    if (g_state.hdcMem) {
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
    
    HDC hdc = GetDC(g_state.hwnd);
    if (!hdc) {
        HandleError(L"Error al obtener DC de ventana");
        return;
    }
    
    // Crear DC compatible con manejo de errores
    g_state.hdcMem = CreateCompatibleDC(hdc);
    if (!g_state.hdcMem) {
        ReleaseDC(g_state.hwnd, hdc);
        HandleError(L"Error al crear DC compatible");
        return;
    }
    
    // Crear bitmap con máxima calidad y manejo de errores
    g_state.hbmMem = CreateCompatibleBitmap(hdc, width, height);
    if (!g_state.hbmMem) {
        DeleteDC(g_state.hdcMem);
        g_state.hdcMem = nullptr;
        ReleaseDC(g_state.hwnd, hdc);
        HandleError(L"Error al crear bitmap compatible - posible memoria insuficiente");
        return;
    }
    
    // Seleccionar bitmap con validación
    g_state.hbmOld = (HBITMAP)SelectObject(g_state.hdcMem, g_state.hbmMem);
    if (!g_state.hbmOld) {
        DeleteObject(g_state.hbmMem);
        DeleteDC(g_state.hdcMem);
        g_state.hdcMem = nullptr;
        g_state.hbmMem = nullptr;
        ReleaseDC(g_state.hwnd, hdc);
        HandleError(L"Error al seleccionar bitmap en DC");
        return;
    }
    
    ReleaseDC(g_state.hwnd, hdc);
}

// Renderizar imagen con soporte de rotación
void RenderImage() {
    if (!g_state.hdcMem || !g_state.imageData) {
        return;
    }
    
    RECT rect;
    GetClientRect(g_state.hwnd, &rect);
    
    // Limpiar con fondo oscuro
    HBRUSH hBrush = CreateSolidBrush(BG_COLOR);
    FillRect(g_state.hdcMem, &rect, hBrush);
    DeleteObject(hBrush);
    
    // Crear Bitmap desde datos de imagen
    Bitmap* bitmap = new Bitmap(g_state.imageWidth, g_state.imageHeight, 
                                 g_state.imageWidth * 4, PixelFormat32bppARGB, 
                                 g_state.imageData);
    
    if (bitmap) {
        Graphics graphics(g_state.hdcMem);
        graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        graphics.SetSmoothingMode(SmoothingModeHighQuality);
        graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        
        // Calcular dimensiones según rotación
        int imgWidth = g_state.imageWidth;
        int imgHeight = g_state.imageHeight;
        
        if (g_state.currentRotation == 90 || g_state.currentRotation == 270) {
            std::swap(imgWidth, imgHeight);
        }
        
        // Calcular posición y tamaño
        int drawX = (int)g_state.offsetX;
        int drawY = (int)g_state.offsetY;
        int drawWidth = (int)(imgWidth * g_state.zoom);
        int drawHeight = (int)(imgHeight * g_state.zoom);
        
        // Aplicar rotación si es necesario
        if (g_state.currentRotation != 0) {
            // Guardar estado actual
            GraphicsState state = graphics.Save();
            
            // Centro de rotación
            float centerX = drawX + drawWidth / 2.0f;
            float centerY = drawY + drawHeight / 2.0f;
            
            // Aplicar transformación de rotación
            graphics.TranslateTransform(centerX, centerY);
            graphics.RotateTransform((float)g_state.currentRotation);
            graphics.TranslateTransform(-centerX, -centerY);
            
            // Dibujar imagen rotada
            graphics.DrawImage(bitmap, drawX, drawY, drawWidth, drawHeight);
            
            // Restaurar estado
            graphics.Restore(state);
        } else {
            // Dibujar imagen sin rotación
            graphics.DrawImage(bitmap, drawX, drawY, drawWidth, drawHeight);
        }
        
        delete bitmap;
    }
    
    // Renderizar OSD
    RenderOSD(g_state.hdcMem);
}

// Procedimiento de ventana
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_state.hwnd = hwnd;
            
            // Crear ventana frameless
            LONG style = GetWindowLong(hwnd, GWL_STYLE);
            style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZE | WS_MAXIMIZE | WS_SYSMENU);
            SetWindowLong(hwnd, GWL_STYLE, style);
            
            // Establecer fondo
            HBRUSH hBrush = CreateSolidBrush(BG_COLOR);
            SetClassLongPtr(hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)hBrush);
            
            // Habilitar drag & drop
            DragAcceptFiles(hwnd, TRUE);
            
            // Iniciar hilo de pre-carga
            StartPrefetchThread();
            
            return 0;
        }
        
        case WM_SIZE: {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);
            CreateDoubleBuffer(width, height);

            if (g_state.imageData && width > 0 && height > 0) {
                FitImageToWindow(width, height);
                EnsureImageVisible();
            }
            
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_GETMINMAXINFO: {
            LPMINMAXINFO minMaxInfo = (LPMINMAXINFO)lParam;
            minMaxInfo->ptMinTrackSize.x = 320;
            minMaxInfo->ptMinTrackSize.y = 220;
            return 0;
        }
        
        case WM_ERASEBKGND: {
            // Evitar parpadeo retornando TRUE sin hacer nada
            return TRUE;
        }
        
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            if (g_state.hdcMem) {
                RenderImage();
                BitBlt(hdc, 0, 0, ps.rcPaint.right, ps.rcPaint.bottom, 
                       g_state.hdcMem, 0, 0, SRCCOPY);
            }
            
            EndPaint(hwnd, &ps);
            return 0;
        }
        
        case WM_KEYDOWN: {
            // Verificar teclas de control
            bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            
            switch (wParam) {
                case VK_LEFT:
                case VK_UP:
                    PreviousImage();
                    break;
                case VK_RIGHT:
                case VK_DOWN:
                case VK_SPACE:
                    NextImage();
                    break;
                case VK_ESCAPE:
                    PostQuitMessage(0);
                    break;
                case 'I':
                case 'i':
                    // Mostrar/ocultar OSD manualmente
                    g_state.showOSD = true;
                    g_state.osdDisplayTime = GetTickCount();
                    InvalidateRect(hwnd, NULL, FALSE);
                    break;
                case 'F':
                case 'f':
                    // Ajustar imagen a ventana
                    RECT rect;
                    GetClientRect(hwnd, &rect);
                    FitImageToWindow(rect.right, rect.bottom);
                    InvalidateRect(hwnd, NULL, FALSE);
                    break;
                case 'R':
                case 'r':
                    // Rotar imagen 90 grados
                    RotateImage();
                    break;
                case VK_F11:
                    // Alternar fullscreen
                    ToggleFullscreen();
                    break;
                case 'C':
                case 'c':
                    if (ctrlPressed) {
                        // Ctrl+C: Copiar imagen al portapapeles
                        CopyImageToClipboard();
                    }
                    break;
                case 'V':
                case 'v':
                    if (ctrlPressed) {
                        // Ctrl+V: Copiar ruta al portapapeles
                        CopyPathToClipboard();
                    }
                    break;
            }
            return 0;
        }
        
        case WM_MOUSEWHEEL: {
            if (!g_state.imageData) return 0;
            
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);
            
            float oldZoom = g_state.zoom;
            float zoomFactor = (delta > 0) ? ZOOM_STEP : (1.0f / ZOOM_STEP);
            float newZoom = std::max(MIN_ZOOM, std::min(MAX_ZOOM, g_state.zoom * zoomFactor));

            if (newZoom == oldZoom) {
                return 0;
            }

            float imageX = (pt.x - g_state.offsetX) / oldZoom;
            float imageY = (pt.y - g_state.offsetY) / oldZoom;
            g_state.zoom = newZoom;
            g_state.offsetX = pt.x - imageX * g_state.zoom;
            g_state.offsetY = pt.y - imageY * g_state.zoom;
            EnsureImageVisible();
            
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        
        case WM_LBUTTONDOWN: {
            g_state.isDragging = true;
            g_state.dragStartX = GET_X_LPARAM(lParam);
            g_state.dragStartY = GET_Y_LPARAM(lParam);
            g_state.dragStartOffsetX = g_state.offsetX;
            g_state.dragStartOffsetY = g_state.offsetY;
            SetCapture(hwnd);
            return 0;
        }
        
        case WM_LBUTTONUP: {
            g_state.isDragging = false;
            ReleaseCapture();
            return 0;
        }
        
        case WM_MOUSEMOVE: {
            if (g_state.isDragging) {
                int x = GET_X_LPARAM(lParam);
                int y = GET_Y_LPARAM(lParam);
                
                g_state.offsetX = g_state.dragStartOffsetX + (x - g_state.dragStartX);
                g_state.offsetY = g_state.dragStartOffsetY + (y - g_state.dragStartY);
                EnsureImageVisible();
                
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        
        case WM_DROPFILES: {
            HDROP hDrop = (HDROP)wParam;
            
            // Obtener número de archivos arrastrados
            UINT fileCount = DragQueryFile(hDrop, 0xFFFFFFFF, NULL, 0);
            
            if (fileCount > 0) {
                // Obtener el primer archivo
                wchar_t filePath[MAX_PATH];
                DragQueryFile(hDrop, 0, filePath, MAX_PATH);
                
                // Verificar si es archivo o carpeta
                DWORD attrs = GetFileAttributes(filePath);
                if (attrs != INVALID_FILE_ATTRIBUTES) {
                    std::wstring newPath(filePath);
                    
                    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
                        // Es una carpeta, escanearla
                        ScanFolderForImages(newPath);
                    } else {
                        // Es un archivo, obtener su carpeta
                        size_t pos = newPath.find_last_of(L'\\');
                        if (pos != std::wstring::npos) {
                            std::wstring folder = newPath.substr(0, pos);
                            ScanFolderForImages(folder);
                            
                            // Encontrar el índice del archivo arrastrado
                            auto it = std::find(g_state.imageFiles.begin(), g_state.imageFiles.end(), newPath);
                            if (it != g_state.imageFiles.end()) {
                                size_t index = std::distance(g_state.imageFiles.begin(), it);
                                LoadImageByIndex(index);
                                RequestPrefetch(index);
                            }
                        }
                    }
                    
                    if (!g_state.imageFiles.empty()) {
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                }
            }
            
            DragFinish(hDrop);
            return 0;
        }
        
        case WM_DESTROY: {
            g_state.prefetchRunning = false;
            if (g_state.prefetchThread.joinable()) {
                g_state.prefetchCV.notify_all();
                g_state.prefetchThread.join();
            }

            FreeCurrentImage();

            if (g_state.hdcMem) {
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

            PostQuitMessage(0);
            return 0;
        }
        
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

// Obtener ruta del ejecutable
std::wstring GetExecutablePath() {
    wchar_t path[MAX_PATH] = {0};
    DWORD len = GetModuleFileName(NULL, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return L"";
    }
    std::wstring fullPath(path);
    size_t pos = fullPath.find_last_of(L'\\');
    if (pos == std::wstring::npos) {
        return L"";
    }
    return fullPath.substr(0, pos);
}

// Solicitar al usuario una carpeta de imágenes cuando la app se inicia sin carpeta válida
bool SelectFolderDialog(HWND hwnd, std::wstring& outFolder) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool success = false;

    BROWSEINFO bi = {};
    wchar_t buffer[MAX_PATH] = {0};

    bi.hwndOwner = hwnd;
    bi.pidlRoot = nullptr;
    bi.pszDisplayName = buffer;
    bi.lpszTitle = L"Selecciona una carpeta con imágenes";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;

    LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
    if (pidl) {
        if (SHGetPathFromIDList(pidl, buffer)) {
            outFolder = buffer;
            success = true;
        }
        CoTaskMemFree(pidl);
    }

    if (SUCCEEDED(hr)) {
        CoUninitialize();
    }

    return success;
}

// Parsear argumentos de línea de comandos para carpeta y modo de ventana
void ParseStartupOptions(LPWSTR lpCmdLine, std::wstring& outFolder, WindowMode& outMode) {
    outFolder = GetExecutablePath();
    outMode = WindowMode::Maximized;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(lpCmdLine, &argc);
    if (!argv) {
        return;
    }

    std::wstring firstPath;
    for (int i = 0; i < argc; ++i) {
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

        if (arg.empty() || arg[0] == L'-') {
            continue;
        }

        if (firstPath.empty()) {
            firstPath = arg;
        }
    }

    LocalFree(argv);

    if (!firstPath.empty()) {
        if (firstPath.front() == L'"' && firstPath.back() == L'"') {
            firstPath = firstPath.substr(1, firstPath.size() - 2);
        }

        DWORD attrs = GetFileAttributes(firstPath.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES) {
            if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
                outFolder = firstPath;
            } else {
                size_t pos = firstPath.find_last_of(L'\\');
                if (pos != std::wstring::npos) {
                    outFolder = firstPath.substr(0, pos);
                }
            }
        }
    }
}

// wWinMain (Unicode entry point expected by the Microsoft CRT)
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(nCmdShow);

    // Activar DPI-aware para evitar que la ventana se cierre o se dibuje mal.
    if (IsWindows10OrGreater()) {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    } else if (IsWindowsVistaOrGreater()) {
        SetProcessDPIAware();
    }

    LogMessage(L"Iniciando ARTPICST");

    // Inicializar GDI+
    if (!InitGDIPlus()) {
        LogMessage(L"Error al inicializar GDI+");
        MessageBox(NULL, L"Error al inicializar GDI+", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    
    // Registrar clase de ventana
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = CLASS_NAME;
    
    if (!RegisterClassEx(&wc)) {
        MessageBox(NULL, L"Error al registrar clase de ventana", L"Error", MB_OK | MB_ICONERROR);
        CleanupGDIPlus();
        return 1;
    }
    
    // Obtener carpeta de imágenes y modo de inicio
    std::wstring folderPath;
    WindowMode startMode = WindowMode::Maximized;
    ParseStartupOptions(lpCmdLine, folderPath, startMode);
    g_state.windowMode = startMode;
    g_state.isFullscreen = (startMode == WindowMode::Fullscreen);
    LogMessage(L"Carpeta inicial: " + folderPath + L" | Modo: " + (startMode == WindowMode::Fullscreen ? L"fullscreen" : (startMode == WindowMode::Normal ? L"normal" : L"maximized")));
    ScanFolderForImages(folderPath);
    
    if (g_state.imageFiles.empty()) {
        std::wstring selectedFolder;
        if (SelectFolderDialog(NULL, selectedFolder)) {
            ScanFolderForImages(selectedFolder);
            LogMessage(L"Carpeta seleccionada manualmente: " + selectedFolder);
        }
    }

    // Guardar la app: no crear ni mostrar ninguna ventana si no hay imágenes válidas.
    if (g_state.imageFiles.empty()) {
        LogMessage(L"No se encontraron imágenes compatibles");
        MessageBox(NULL, L"No se encontraron imágenes compatibles en la carpeta seleccionada. Abre la app desde una carpeta con JPG, PNG, BMP, WebP o TGA.", L"ARTPICST", MB_OK | MB_ICONINFORMATION);
        CleanupGDIPlus();
        return 0;
    }
    
    // Crear ventana en modo normal maximizado, sin forzar fullscreen
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    DWORD windowStyle = WS_POPUP | WS_VISIBLE;
    int x = 0;
    int y = 0;
    int width = screenWidth;
    int height = screenHeight;

    if (startMode == WindowMode::Normal) {
        windowStyle = WS_POPUP | WS_VISIBLE;
        width = std::max(900, screenWidth - 80);
        height = std::max(600, screenHeight - 80);
        x = (screenWidth - width) / 2;
        y = (screenHeight - height) / 2;
    } else if (startMode == WindowMode::Maximized) {
        windowStyle = WS_POPUP | WS_VISIBLE;
        x = 0;
        y = 0;
        width = screenWidth;
        height = screenHeight;
    }
    
    HWND hwnd = CreateWindowEx(
        0,
        CLASS_NAME,
        L"ARTPICST",
        windowStyle,
        x, y, width, height,
        NULL, NULL, hInstance, NULL
    );
    
    if (!hwnd) {
        MessageBox(NULL, L"Error al crear ventana", L"Error", MB_OK | MB_ICONERROR);
        CleanupGDIPlus();
        return 1;
    }
    
    // Cargar primera imagen
    LoadImageByIndex(0);
    
    // Mostrar ventana en el modo solicitado, sin forzar fullscreen a la fuerza
    if (startMode == WindowMode::Normal) {
        ShowWindow(hwnd, SW_SHOWNORMAL);
    } else if (startMode == WindowMode::Maximized) {
        ShowWindow(hwnd, SW_SHOWMAXIMIZED);
    } else {
        ShowWindow(hwnd, SW_SHOWMAXIMIZED);
    }
    UpdateWindow(hwnd);
    
    // Bucle de mensajes
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    // Limpieza
    FreeCurrentImage();
    CleanupGDIPlus();
    
    return (int)msg.wParam;
}