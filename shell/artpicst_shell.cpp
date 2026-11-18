// ============================================================================
// artpicst_shell.cpp — Extensión de shell de ARTPICST (DLL COM en proceso)
// ----------------------------------------------------------------------------
// Proporciona al Explorador de Windows dos integraciones REALES para los
// formatos que abre el visor:
//
//   1. MINIATURAS (IThumbnailProvider + IInitializeWithStream): las vistas
//      "Iconos grandes", "Mosaicos" y "Contenido" muestran la imagen en lugar
//      del icono genérico. La miniatura conserva la transparencia (WTSAT_ARGB)
//      y respeta la relación de aspecto.
//
//   2. VISTA PREVIA (IPreviewHandler + IInitializeWithStream + IObjectWithSite
//      + IOleWindow): el panel de vista previa del Explorador (Alt+P) muestra
//      la imagen escalada, centrada y con fondo del sistema, con suavizado
//      HALFTONE y composición alfa correcta.
//
// DECODIFICACIÓN
//   · Primero Windows Imaging Component (WIC): la misma ruta que usa el visor
//     para TIFF/ICO/WebP/HEIC/AVIF/JXR/RAW. El escalado se hace EN EL DECODER
//     (IWICBitmapScaler), así una foto de 50 MP nunca se materializa entera, y
//     la orientación EXIF se aplica EXPLÍCITAMENTE (WIC no la aplica solo):
//     sin ese paso, una foto vertical de móvil se vería tumbada en el
//     Explorador.
//   · Si no hay códec WIC (PSD, TGA, PNM, PIC…), stb_image como respaldo.
//   · Todo el trabajo se hace sobre un búfer en memoria acotado: un archivo
//     corrupto o gigante no puede colgar el proceso anfitrión.
//
// SEGURIDAD Y AISLAMIENTO
//   · El proveedor de miniaturas NO pide desactivar el aislamiento de proceso:
//     Explorer puede ejecutarlo en el host aislado (prevhost/dllhost).
//   · No se escribe nada en disco, no se crean ventanas de nivel superior y no
//     se muestra UI desde el proveedor de miniaturas.
//   · Todos los buffers se validan antes de escribirse (sin desbordamientos).
//
// REGISTRO
//   DllRegisterServer/DllUnregisterServer (regsvr32) y, además, dos exports
//   propios que usa el instalador para poder registrar en HKLM o HKCU según la
//   elevación real:
//       ArtpicstRegisterShell(HKEY root)   -> HRESULT
//       ArtpicstUnregisterShell(HKEY root) -> HRESULT
//   El registro asocia los manejadores por extensión mediante
//   SystemFileAssociations\<ext>\ShellEx\{GUID}, que NO secuestra el programa
//   predeterminado ni las miniaturas de otros visores cuando el usuario elige
//   otro handler.
// ============================================================================

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
#include <objbase.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <propsys.h>
#include <propidl.h>
#include <thumbcache.h>
#include <wincodec.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <vector>

// stb_image como respaldo para formatos sin códec WIC instalado.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "stb_image.h"

#ifdef _MSC_VER
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "advapi32.lib")
#endif

namespace {

// ---------------------------------------------------------------------------
// Identidad COM (GUIDs estables: NO cambiarlos sin actualizar el instalador,
// que los registra por su forma textual).
// ---------------------------------------------------------------------------
// {6C7E9E51-3E8B-4E2C-9A5B-1D2F4A7C8B10}
const CLSID CLSID_ArtpicstThumbnail =
    { 0x6C7E9E51, 0x3E8B, 0x4E2C, { 0x9A, 0x5B, 0x1D, 0x2F, 0x4A, 0x7C, 0x8B, 0x10 } };
// {0F3A6B24-7D1C-4B58-9E47-2C8D5A1F6E33}
const CLSID CLSID_ArtpicstPreview =
    { 0x0F3A6B24, 0x7D1C, 0x4B58, { 0x9E, 0x47, 0x2C, 0x8D, 0x5A, 0x1F, 0x6E, 0x33 } };

constexpr wchar_t kThumbClsidText[]   = L"{6C7E9E51-3E8B-4E2C-9A5B-1D2F4A7C8B10}";
constexpr wchar_t kPreviewClsidText[] = L"{0F3A6B24-7D1C-4B58-9E47-2C8D5A1F6E33}";

// CLSIDs de los tipos de manejador del shell (valores del sistema, fijos).
constexpr wchar_t kThumbHandlerKey[]   = L"{E357FCCD-A995-4576-B01F-234630154E96}";
constexpr wchar_t kPreviewHandlerKey[] = L"{8895B1C6-B41F-4C1C-A562-0D564250836F}";
// AppID del host de vista previa (PrevHost.exe): ejecuta el handler fuera de
// proceso, que es el modelo recomendado por Microsoft para IPreviewHandler.
constexpr wchar_t kPrevHostAppId[]     = L"{6D2B5079-2F0B-48DD-AB7F-97CEC514D30B}";

constexpr wchar_t kThumbFriendly[]   = L"ARTPICST Thumbnail Provider";
constexpr wchar_t kPreviewFriendly[] = L"ARTPICST Preview Handler";

// Extensiones con integración de shell. Es la misma lista que ofrece el visor
// (installer/artpicst_installer.cpp la usa para «Abrir con»). Los formatos de
// metarchivo (EMF/WMF) quedan fuera a propósito: no son imágenes rasterizables
// por WIC ni por stb.
const wchar_t* const kShellExtensions[] = {
    L".jpg", L".jpeg", L".jpe", L".jfif", L".jif",
    L".png", L".apng", L".bmp", L".dib", L".rle", L".gif",
    L".tif", L".tiff", L".webp", L".heic", L".heif", L".hif",
    L".avif", L".jxl", L".jxr", L".wdp", L".hdp", L".ico",
    L".cur", L".icon", L".tga", L".tpic", L".psd", L".hdr",
    L".pic", L".pnm", L".ppm", L".pgm", L".pbm", L".jp2",
    L".j2k", L".jpx", L".exif", L".dng", L".cr2", L".cr3",
    L".nef", L".arw", L".orf", L".rw2", L".raf", L".sr2",
    L".kdc", L".raw",
};
constexpr size_t kShellExtensionCount =
    sizeof(kShellExtensions) / sizeof(kShellExtensions[0]);

HINSTANCE g_hInstance = nullptr;
volatile LONG g_objectCount = 0;   // objetos vivos (DllCanUnloadNow)
volatile LONG g_serverLocks = 0;   // IClassFactory::LockServer

// ---------------------------------------------------------------------------
// Utilidades de memoria y decodificación
// ---------------------------------------------------------------------------
struct DecodedImage {
    std::vector<BYTE> pixels;   // BGRA premultiplicado, filas de arriba abajo
    UINT width = 0;
    UINT height = 0;

    void Clear() {
        pixels.clear();
        pixels.shrink_to_fit();
        width = 0;
        height = 0;
    }
};

// Tope de entrada: el visor acepta archivos grandes, pero un handler del shell
// no debe reservar media memoria del sistema por una miniatura.
constexpr size_t kMaxInputBytes = 512ull * 1024ull * 1024ull;
constexpr UINT   kMaxPreviewSide = 4096;

// Lee el stream completo. El búfer es del handler y nunca se comparte.
HRESULT ReadStreamToBuffer(IStream* stream, std::vector<BYTE>& out) {
    out.clear();
    if (!stream) return E_POINTER;

    LARGE_INTEGER zero{};
    stream->Seek(zero, STREAM_SEEK_SET, nullptr);   // best-effort: algunos streams no buscan

    BYTE chunk[64 * 1024];
    for (;;) {
        ULONG read = 0;
        const HRESULT hr = stream->Read(chunk, static_cast<ULONG>(sizeof(chunk)), &read);
        if (hr == S_FALSE) break;          // fin de stream
        if (FAILED(hr)) return hr;
        if (read == 0) break;
        if (out.size() + read > kMaxInputBytes) return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        out.insert(out.end(), chunk, chunk + read);
    }
    return out.empty() ? E_FAIL : S_OK;
}

void FitSize(UINT srcW, UINT srcH, UINT maxSide, UINT& dstW, UINT& dstH) {
    if (srcW == 0 || srcH == 0 || maxSide == 0) {
        dstW = dstH = 1;
        return;
    }
    const double side = static_cast<double>((std::max)(srcW, srcH));
    const double scale = (side > static_cast<double>(maxSide))
                             ? static_cast<double>(maxSide) / side
                             : 1.0;
    dstW = static_cast<UINT>((std::max)(1L, std::lround(static_cast<double>(srcW) * scale)));
    dstH = static_cast<UINT>((std::max)(1L, std::lround(static_cast<double>(srcH) * scale)));
}

// Orientación EXIF (tag 274) de la imagen. Devuelve 1 (sin transformar) si el
// archivo no la declara, si el valor no es válido o si el contenedor no expone
// metadatos (BMP, TGA, PNM…). Todas las consultas se prueban porque el árbol de
// metadatos depende del contenedor: TIFF/HEIC usan /ifd, JPEG usa /app1/ifd.
UINT ReadExifOrientation(IWICBitmapFrameDecode* frame) {
    if (!frame) return 1;
    IWICMetadataQueryReader* reader = nullptr;
    if (FAILED(frame->GetMetadataQueryReader(&reader)) || !reader) return 1;

    const wchar_t* const kQueries[] = {
        L"/app1/ifd/{ushort=274}",
        L"/ifd/{ushort=274}",
        L"/app1/ifd/exif/{ushort=274}",
    };
    UINT orientation = 1;
    for (const wchar_t* query : kQueries) {
        PROPVARIANT value{};   // cero = variante vacía (equivalente a PropVariantInit)
        bool found = false;
        if (SUCCEEDED(reader->GetMetadataByName(query, &value))) {
            if (value.vt == VT_UI2) { orientation = value.uiVal; found = true; }
            else if (value.vt == VT_UI4) { orientation = value.ulVal; found = true; }
            else if (value.vt == VT_I2) {
                orientation = static_cast<UINT>(value.iVal);
                found = true;
            }
        }
        PropVariantClear(&value);
        if (found) break;
    }
    reader->Release();

    // Solo los valores 1..8 están definidos por EXIF.
    return (orientation >= 1 && orientation <= 8) ? orientation : 1;
}

// Transformación WIC equivalente a cada orientación EXIF. WIC admite combinar
// una rotación con un volteo, que es justo lo que necesitan 5 y 7 (transponer
// y transponer invertido).
WICBitmapTransformOptions TransformForOrientation(UINT orientation) {
    switch (orientation) {
        case 2: return static_cast<WICBitmapTransformOptions>(WICBitmapTransformFlipHorizontal);
        case 3: return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate180);
        case 4: return static_cast<WICBitmapTransformOptions>(WICBitmapTransformFlipVertical);
        case 5: return static_cast<WICBitmapTransformOptions>(
                        WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal);
        case 6: return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90);
        case 7: return static_cast<WICBitmapTransformOptions>(
                        WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal);
        case 8: return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270);
        default: return WICBitmapTransformRotate0;
    }
}

// Decodifica con WIC, escala EN EL DECODIFICADOR y deja la imagen ya orientada.
HRESULT DecodeWithWic(const std::vector<BYTE>& bytes, UINT maxSide, DecodedImage& out) {
    if (bytes.empty() || bytes.size() > 0xFFFFFFFFull) return E_INVALIDARG;

    IStream* memStream = SHCreateMemStream(bytes.data(), static_cast<UINT>(bytes.size()));
    if (!memStream) return E_OUTOFMEMORY;

    IWICImagingFactory*   factory   = nullptr;
    IWICBitmapDecoder*    decoder   = nullptr;
    IWICBitmapFrameDecode* frame    = nullptr;
    IWICBitmapScaler*     scaler    = nullptr;
    IWICBitmapFlipRotator* rotator  = nullptr;
    IWICFormatConverter*  converter = nullptr;

    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IWICImagingFactory,
                                  reinterpret_cast<void**>(&factory));
    if (SUCCEEDED(hr)) {
        hr = factory->CreateDecoderFromStream(memStream, nullptr,
                                              WICDecodeMetadataCacheOnDemand, &decoder);
    }
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);

    UINT frameW = 0, frameH = 0;
    if (SUCCEEDED(hr)) hr = frame->GetSize(&frameW, &frameH);

    UINT targetW = frameW, targetH = frameH;
    if (SUCCEEDED(hr)) {
        FitSize(frameW, frameH, maxSide, targetW, targetH);
        IWICBitmapSource* source = frame;
        if (targetW != frameW || targetH != frameH) {
            hr = factory->CreateBitmapScaler(&scaler);
            if (SUCCEEDED(hr)) {
                hr = scaler->Initialize(frame, targetW, targetH,
                                        WICBitmapInterpolationModeFant);
            }
            if (SUCCEEDED(hr)) source = scaler;
        }
        // Orientación EXIF ANTES de convertir el formato (el rotador ya trabaja
        // sobre la imagen reducida: rotar 256×384 cuesta nada).
        const UINT orientation = ReadExifOrientation(frame);
        if (SUCCEEDED(hr) && orientation != 1) {
            hr = factory->CreateBitmapFlipRotator(&rotator);
            if (SUCCEEDED(hr)) {
                hr = rotator->Initialize(source, TransformForOrientation(orientation));
            }
            if (SUCCEEDED(hr)) source = rotator;
        }
        if (SUCCEEDED(hr)) hr = factory->CreateFormatConverter(&converter);
        if (SUCCEEDED(hr)) {
            // Premultiplicado: lo que esperan AlphaBlend y la caché de miniaturas.
            hr = converter->Initialize(source, GUID_WICPixelFormat32bppPBGRA,
                                       WICBitmapDitherTypeNone, nullptr, 0.0,
                                       WICBitmapPaletteTypeCustom);
        }
        // El tamaño real de salida lo fija la fuente ya transformada: al girar
        // 90°/270° el ancho y el alto se intercambian.
        UINT outW = 0, outH = 0;
        if (SUCCEEDED(hr)) hr = converter->GetSize(&outW, &outH);
        if (SUCCEEDED(hr)) {
            const size_t stride = static_cast<size_t>(outW) * 4u;
            const size_t total = stride * static_cast<size_t>(outH);
            if (total == 0 || total > 0xFFFFFFFFull) {
                hr = E_OUTOFMEMORY;
            } else {
                out.pixels.resize(total);
                hr = converter->CopyPixels(nullptr, static_cast<UINT>(stride),
                                           static_cast<UINT>(total), out.pixels.data());
                if (SUCCEEDED(hr)) {
                    out.width = outW;
                    out.height = outH;
                } else {
                    out.Clear();
                }
            }
        }
    }

    if (converter) converter->Release();
    if (rotator)   rotator->Release();
    if (scaler)    scaler->Release();
    if (frame)     frame->Release();
    if (decoder)   decoder->Release();
    if (factory)   factory->Release();
    memStream->Release();
    return hr;
}

// Declaración adelantada: el respaldo stb reduce sin copias intermedias y
// DownscaleBgra se define después (misma unidad de traducción).
void DownscaleBgra(const BYTE* src, UINT srcW, UINT srcH, UINT dstW, UINT dstH,
                   std::vector<BYTE>& out);

// Tope del respaldo stb. Por encima de este lado el handler prefiere no dar
// miniatura antes que reservar cientos de MB en el proceso del Explorador
// (8192² supone ~268 MB de búfer decodificado).
constexpr int kStbMaxSide = 8192;

// Respaldos sin códec WIC instalado (PSD, TGA, PNM/PPM/PGM, PIC, BMP raros).
// La conversión RGBA -> BGRA premultiplicado se hace EN SITIO y la reducción va
// directa del búfer de stb al destino: nunca se materializa una copia a tamaño
// completo. La orientación no se aplica aquí porque estos contenedores no
// declaran EXIF (PSD y TGA usan sus propios campos y stb no los expone).
bool DecodeWithStb(const std::vector<BYTE>& bytes, UINT maxSide, DecodedImage& out) {
    if (bytes.empty() || bytes.size() > static_cast<size_t>(INT32_MAX)) return false;

    int w = 0, h = 0, components = 0;
    if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                               &w, &h, &components)) {
        return false;
    }
    // Se rechaza ANTES de reservar: una cabecera con dimensiones absurdas no
    // puede provocar una reserva gigantesca.
    if (w <= 0 || h <= 0 || w > kStbMaxSide || h > kStbMaxSide) return false;

    unsigned char* rgba = stbi_load_from_memory(bytes.data(),
                                                static_cast<int>(bytes.size()),
                                                &w, &h, &components, 4);
    if (!rgba) return false;

    const size_t pixels = static_cast<size_t>(w) * static_cast<size_t>(h);
    for (size_t i = 0; i < pixels; ++i) {
        unsigned char* p = rgba + i * 4u;
        const unsigned int a = p[3];
        const unsigned char r = p[0];
        const unsigned char g = p[1];
        const unsigned char b = p[2];
        p[0] = static_cast<unsigned char>((b * a + 127u) / 255u);   // B premult.
        p[1] = static_cast<unsigned char>((g * a + 127u) / 255u);   // G premult.
        p[2] = static_cast<unsigned char>((r * a + 127u) / 255u);   // R premult.
    }

    const UINT srcW = static_cast<UINT>(w);
    const UINT srcH = static_cast<UINT>(h);
    UINT dstW = srcW, dstH = srcH;
    FitSize(srcW, srcH, maxSide, dstW, dstH);
    DownscaleBgra(rgba, srcW, srcH, dstW, dstH, out.pixels);
    out.width = dstW;
    out.height = dstH;
    stbi_image_free(rgba);
    return true;
}

// Reducción por media de área en espacio premultiplicado (correcta con alfa).
// Trabaja sobre un búfer BGRA crudo para poder reducir sin copias intermedias.
void DownscaleBgra(const BYTE* src, UINT srcW, UINT srcH, UINT dstW, UINT dstH,
                   std::vector<BYTE>& out) {
    if (!src || srcW == 0 || srcH == 0 || dstW == 0 || dstH == 0) {
        out.clear();
        return;
    }
    out.assign(static_cast<size_t>(dstW) * dstH * 4u, 0);
    for (UINT y = 0; y < dstH; ++y) {
        const UINT y0 = static_cast<UINT>((static_cast<unsigned long long>(y) * srcH) / dstH);
        UINT y1 = static_cast<UINT>((static_cast<unsigned long long>(y + 1) * srcH) / dstH);
        if (y1 <= y0) y1 = y0 + 1;
        if (y1 > srcH) y1 = srcH;
        for (UINT x = 0; x < dstW; ++x) {
            const UINT x0 = static_cast<UINT>((static_cast<unsigned long long>(x) * srcW) / dstW);
            UINT x1 = static_cast<UINT>((static_cast<unsigned long long>(x + 1) * srcW) / dstW);
            if (x1 <= x0) x1 = x0 + 1;
            if (x1 > srcW) x1 = srcW;

            unsigned long long sumB = 0, sumG = 0, sumR = 0, sumA = 0;
            unsigned long long samples = 0;
            for (UINT sy = y0; sy < y1; ++sy) {
                const BYTE* row = src + static_cast<size_t>(sy) * srcW * 4u;
                for (UINT sx = x0; sx < x1; ++sx) {
                    const BYTE* p = row + static_cast<size_t>(sx) * 4u;
                    sumB += p[0];
                    sumG += p[1];
                    sumR += p[2];
                    sumA += p[3];
                    ++samples;
                }
            }
            if (samples == 0) samples = 1;
            BYTE* d = out.data() + (static_cast<size_t>(y) * dstW + x) * 4u;
            d[0] = static_cast<BYTE>(sumB / samples);
            d[1] = static_cast<BYTE>(sumG / samples);
            d[2] = static_cast<BYTE>(sumR / samples);
            d[3] = static_cast<BYTE>(sumA / samples);
        }
    }
}

void DownscaleBgra(const DecodedImage& src, UINT dstW, UINT dstH, std::vector<BYTE>& out) {
    DownscaleBgra(src.pixels.data(), src.width, src.height, dstW, dstH, out);
}

// Decodifica al lado máximo indicado (0 = tamaño nativo acotado por kMaxPreviewSide).
HRESULT DecodeImage(const std::vector<BYTE>& bytes, UINT maxSide, DecodedImage& out) {
    out.Clear();
    if (maxSide == 0) maxSide = kMaxPreviewSide;

    const HRESULT hr = DecodeWithWic(bytes, maxSide, out);
    if (SUCCEEDED(hr) && out.width > 0 && out.height > 0) return S_OK;

    out.Clear();
    // stb entrega la imagen ya reducida al lado pedido (ver DecodeWithStb).
    if (!DecodeWithStb(bytes, maxSide, out)) return E_FAIL;
    return S_OK;
}

HBITMAP CreateDib32(UINT width, UINT height, void** bits) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(width);
    info.bmiHeader.biHeight = -static_cast<LONG>(height);   // de arriba abajo
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    return CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, bits, nullptr, 0);
}

// ---------------------------------------------------------------------------
// Proveedor de MINIATURAS
// ---------------------------------------------------------------------------
class ThumbnailProvider final : public IThumbnailProvider,
                                public IInitializeWithStream {
public:
    ThumbnailProvider() { InterlockedIncrement(&g_objectCount); }
    ~ThumbnailProvider() {
        if (m_stream) m_stream->Release();
        InterlockedDecrement(&g_objectCount);
    }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IThumbnailProvider) {
            *ppv = static_cast<IThumbnailProvider*>(this);
        } else if (riid == IID_IInitializeWithStream) {
            *ppv = static_cast<IInitializeWithStream*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&m_ref)); }
    STDMETHODIMP_(ULONG) Release() override {
        const LONG refs = InterlockedDecrement(&m_ref);
        if (refs == 0) delete this;
        return static_cast<ULONG>(refs);
    }

    // IInitializeWithStream
    STDMETHODIMP Initialize(IStream* stream, DWORD) override {
        if (!stream) return E_POINTER;
        if (m_stream) {
            m_stream->Release();
            m_stream = nullptr;
        }
        m_stream = stream;
        m_stream->AddRef();
        return S_OK;
    }

    // IThumbnailProvider
    STDMETHODIMP GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha) override {
        if (!phbmp || !pdwAlpha) return E_POINTER;
        *phbmp = nullptr;
        *pdwAlpha = WTSAT_ARGB;

        if (!m_stream) return E_UNEXPECTED;
        UINT side = (cx == 0) ? 256u : cx;
        if (side > 1024u) side = 1024u;

        std::vector<BYTE> bytes;
        HRESULT hr = ReadStreamToBuffer(m_stream, bytes);
        if (FAILED(hr)) return hr;

        DecodedImage image;
        hr = DecodeImage(bytes, side, image);
        if (FAILED(hr) || image.width == 0 || image.height == 0) return E_FAIL;

        UINT outW = image.width, outH = image.height;
        FitSize(image.width, image.height, side, outW, outH);
        std::vector<BYTE> scaled;
        const BYTE* source = image.pixels.data();
        if (outW != image.width || outH != image.height) {
            DownscaleBgra(image, outW, outH, scaled);
            source = scaled.data();
        }

        void* bits = nullptr;
        HBITMAP bitmap = CreateDib32(outW, outH, &bits);
        if (!bitmap) return E_OUTOFMEMORY;
        if (!bits) {
            DeleteObject(bitmap);
            return E_OUTOFMEMORY;
        }
        memcpy(bits, source, static_cast<size_t>(outW) * outH * 4u);
        *phbmp = bitmap;
        return S_OK;
    }

private:
    LONG m_ref = 1;
    IStream* m_stream = nullptr;
};

// ---------------------------------------------------------------------------
// Manejador de VISTA PREVIA
// ---------------------------------------------------------------------------
constexpr wchar_t kPreviewClass[] = L"ARTPICST.PreviewPane";

std::atomic<bool> g_previewClassRegistered{ false };

LRESULT CALLBACK PreviewPaneProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool EnsurePreviewClass() {
    if (g_previewClassRegistered.load(std::memory_order_acquire)) return true;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = PreviewPaneProc;
    wc.hInstance = g_hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;          // el fondo se pinta en WM_PAINT
    wc.lpszClassName = kPreviewClass;
    const ATOM atom = RegisterClassExW(&wc);
    const bool ok = (atom != 0) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    if (ok) g_previewClassRegistered.store(true, std::memory_order_release);
    return ok;
}

class PreviewHandler final : public IPreviewHandler,
                             public IInitializeWithStream,
                             public IObjectWithSite,
                             public IOleWindow {
public:
    PreviewHandler() { InterlockedIncrement(&g_objectCount); }
    ~PreviewHandler() {
        DestroyPane();
        if (m_stream) m_stream->Release();
        if (m_frame) m_frame->Release();
        InterlockedDecrement(&g_objectCount);
    }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IPreviewHandler) {
            *ppv = static_cast<IPreviewHandler*>(this);
        } else if (riid == IID_IInitializeWithStream) {
            *ppv = static_cast<IInitializeWithStream*>(this);
        } else if (riid == IID_IObjectWithSite) {
            *ppv = static_cast<IObjectWithSite*>(this);
        } else if (riid == IID_IOleWindow) {
            *ppv = static_cast<IOleWindow*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&m_ref)); }
    STDMETHODIMP_(ULONG) Release() override {
        const LONG refs = InterlockedDecrement(&m_ref);
        if (refs == 0) delete this;
        return static_cast<ULONG>(refs);
    }

    // IInitializeWithStream
    STDMETHODIMP Initialize(IStream* stream, DWORD) override {
        if (!stream) return E_POINTER;
        if (m_stream) {
            m_stream->Release();
            m_stream = nullptr;
        }
        m_stream = stream;
        m_stream->AddRef();
        return S_OK;
    }

    // IObjectWithSite
    STDMETHODIMP SetSite(IUnknown* site) override {
        if (m_frame) {
            m_frame->Release();
            m_frame = nullptr;
        }
        if (!site) return S_OK;
        // El marco es opcional: el handler funciona sin él (sin atajos de teclado).
        if (FAILED(site->QueryInterface(IID_IPreviewHandlerFrame,
                                        reinterpret_cast<void**>(&m_frame)))) {
            m_frame = nullptr;
        }
        return S_OK;
    }
    STDMETHODIMP GetSite(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        return m_frame ? m_frame->QueryInterface(riid, ppv) : E_NOINTERFACE;
    }

    // IOleWindow
    STDMETHODIMP GetWindow(HWND* phwnd) override {
        if (!phwnd) return E_POINTER;
        *phwnd = m_pane;
        return m_pane ? S_OK : E_FAIL;
    }
    STDMETHODIMP ContextSensitiveHelp(BOOL) override { return E_NOTIMPL; }

    // IPreviewHandler
    STDMETHODIMP SetWindow(HWND hwndParent, const RECT* rect) override {
        if (!hwndParent) return E_INVALIDARG;
        m_parent = hwndParent;
        // El host puede haber destruido la ventana del panel (o la padre) entre
        // dos llamadas: sin esta comprobación el HWND guardado quedaría colgado
        // y se pintaría sobre una ventana reciclada por otro componente.
        if (m_pane && !IsWindow(m_pane)) m_pane = nullptr;
        if (!EnsurePreviewClass()) return E_FAIL;
        if (!m_pane) {
            m_pane = CreateWindowExW(0, kPreviewClass, L"", WS_CHILD | WS_VISIBLE,
                                     0, 0, 16, 16, m_parent, nullptr, g_hInstance, this);
            if (!m_pane) return E_FAIL;
        } else if (GetParent(m_pane) != m_parent) {
            SetParent(m_pane, m_parent);
        }
        if (rect) m_rect = *rect;
        ApplyRect();
        return S_OK;
    }

    STDMETHODIMP SetRect(const RECT* rect) override {
        if (!rect) return E_INVALIDARG;
        m_rect = *rect;
        ApplyRect();
        return S_OK;
    }

    STDMETHODIMP DoPreview() override {
        if (!m_stream) return E_UNEXPECTED;

        std::vector<BYTE> bytes;
        HRESULT hr = ReadStreamToBuffer(m_stream, bytes);
        if (FAILED(hr)) return hr;

        DecodedImage image;
        hr = DecodeImage(bytes, kMaxPreviewSide, image);
        if (FAILED(hr) || image.width == 0 || image.height == 0) return E_FAIL;

        void* bits = nullptr;
        HBITMAP bitmap = CreateDib32(image.width, image.height, &bits);
        if (!bitmap || !bits) {
            if (bitmap) DeleteObject(bitmap);
            return E_OUTOFMEMORY;
        }
        memcpy(bits, image.pixels.data(), static_cast<size_t>(image.width) * image.height * 4u);

        if (m_bitmap) DeleteObject(m_bitmap);
        m_bitmap = bitmap;
        m_bitmapW = image.width;
        m_bitmapH = image.height;

        if (m_pane && IsWindow(m_pane)) InvalidateRect(m_pane, nullptr, FALSE);
        return S_OK;
    }

    STDMETHODIMP Unload() override {
        DestroyPane();
        if (m_bitmap) {
            DeleteObject(m_bitmap);
            m_bitmap = nullptr;
        }
        m_bitmapW = m_bitmapH = 0;
        if (m_stream) {
            m_stream->Release();
            m_stream = nullptr;
        }
        return S_OK;
    }

    STDMETHODIMP SetFocus() override {
        if (!m_pane) return E_FAIL;
        ::SetFocus(m_pane);
        return S_OK;
    }

    STDMETHODIMP QueryFocus(HWND* phwnd) override {
        if (!phwnd) return E_POINTER;
        *phwnd = GetFocus();
        return *phwnd ? S_OK : E_FAIL;
    }

    STDMETHODIMP TranslateAccelerator(MSG*) override { return S_FALSE; }

    // --- Pintado del panel -------------------------------------------------
    void PaintPane(HWND hwnd) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        if (!dc) {
            EndPaint(hwnd, &paint);
            return;
        }
        RECT client{};
        GetClientRect(hwnd, &client);
        const int clientW = client.right - client.left;
        const int clientH = client.bottom - client.top;

        // Fondo del sistema (respeta el tema claro/oscuro del Explorador).
        FillRect(dc, &client, GetSysColorBrush(COLOR_WINDOW));

        if (m_bitmap && m_bitmapW > 0 && m_bitmapH > 0 && clientW > 0 && clientH > 0) {
            const double scaleX = static_cast<double>(clientW) / static_cast<double>(m_bitmapW);
            const double scaleY = static_cast<double>(clientH) / static_cast<double>(m_bitmapH);
            const double scale = (std::min)(scaleX, scaleY);
            int dstW = static_cast<int>(std::lround(static_cast<double>(m_bitmapW) * scale));
            int dstH = static_cast<int>(std::lround(static_cast<double>(m_bitmapH) * scale));
            if (dstW < 1) dstW = 1;
            if (dstH < 1) dstH = 1;
            const int dstX = client.left + (clientW - dstW) / 2;
            const int dstY = client.top + (clientH - dstH) / 2;

            HDC mem = CreateCompatibleDC(dc);
            if (mem) {
                HGDIOBJ old = SelectObject(mem, m_bitmap);
                SetStretchBltMode(dc, HALFTONE);
                SetBrushOrgEx(dc, 0, 0, nullptr);
                BLENDFUNCTION blend{};
                blend.BlendOp = AC_SRC_OVER;
                blend.SourceConstantAlpha = 255;
                blend.AlphaFormat = AC_SRC_ALPHA;   // BGRA premultiplicado
                AlphaBlend(dc, dstX, dstY, dstW, dstH, mem, 0, 0,
                           static_cast<int>(m_bitmapW), static_cast<int>(m_bitmapH), blend);
                SelectObject(mem, old);
                DeleteDC(mem);
            }
        }
        EndPaint(hwnd, &paint);
    }

private:
    void ApplyRect() {
        if (!m_pane) return;
        RECT parentClient{};
        GetClientRect(m_parent, &parentClient);
        RECT target = m_rect;
        // Normaliza: el host puede enviar coordenadas relativas al padre.
        if (target.right <= target.left || target.bottom <= target.top) {
            target = parentClient;
        }
        SetWindowPos(m_pane, nullptr, target.left, target.top,
                     target.right - target.left, target.bottom - target.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(m_pane, nullptr, FALSE);
    }

    void DestroyPane() {
        if (m_pane && IsWindow(m_pane)) {
            SetWindowLongPtrW(m_pane, GWLP_USERDATA, 0);
            DestroyWindow(m_pane);
        }
        m_pane = nullptr;
        m_parent = nullptr;
    }

    LONG m_ref = 1;
    IStream* m_stream = nullptr;
    IPreviewHandlerFrame* m_frame = nullptr;
    HWND m_parent = nullptr;
    HWND m_pane = nullptr;
    RECT m_rect{};
    HBITMAP m_bitmap = nullptr;
    UINT m_bitmapW = 0;
    UINT m_bitmapH = 0;
};

LRESULT CALLBACK PreviewPaneProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PreviewHandler* self = reinterpret_cast<PreviewHandler*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return TRUE;
        }
        case WM_ERASEBKGND:
            return 1;   // sin parpadeo: el fondo se pinta en WM_PAINT
        case WM_SIZE:
            // El host puede mover o redimensionar el panel sin volver a llamar a
            // SetRect; sin esto el panel se quedaría con el fondo en blanco.
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_PAINT:
            if (self) self->PaintPane(hwnd);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Fábrica de clases COM
// ---------------------------------------------------------------------------
class ClassFactory final : public IClassFactory {
public:
    explicit ClassFactory(REFCLSID clsid) : m_clsid(clsid) {}
    ~ClassFactory() = default;

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid != IID_IUnknown && riid != IID_IClassFactory) return E_NOINTERFACE;
        *ppv = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&m_ref)); }
    STDMETHODIMP_(ULONG) Release() override {
        const LONG refs = InterlockedDecrement(&m_ref);
        if (refs == 0) delete this;
        return static_cast<ULONG>(refs);
    }

    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;

        IUnknown* object = nullptr;
        if (m_clsid == CLSID_ArtpicstThumbnail) {
            object = static_cast<IThumbnailProvider*>(new (std::nothrow) ThumbnailProvider());
        } else if (m_clsid == CLSID_ArtpicstPreview) {
            object = static_cast<IPreviewHandler*>(new (std::nothrow) PreviewHandler());
        } else {
            return CLASS_E_CLASSNOTAVAILABLE;
        }
        if (!object) return E_OUTOFMEMORY;

        const HRESULT hr = object->QueryInterface(riid, ppv);
        object->Release();
        return hr;
    }

    STDMETHODIMP LockServer(BOOL lock) override {
        if (lock) InterlockedIncrement(&g_serverLocks);
        else      InterlockedDecrement(&g_serverLocks);
        return S_OK;
    }

private:
    LONG m_ref = 1;
    CLSID m_clsid;
};

// ---------------------------------------------------------------------------
// Registro
// ---------------------------------------------------------------------------
bool SetRegString(HKEY root, const std::wstring& subKey, const wchar_t* valueName,
                  const std::wstring& value) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, subKey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const LSTATUS status = RegSetValueExW(key, valueName, 0, REG_SZ,
                                          reinterpret_cast<const BYTE*>(value.c_str()),
                                          bytes);
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

std::wstring ModulePath() {
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD len = GetModuleFileNameW(g_hInstance, buffer.data(),
                                             static_cast<DWORD>(buffer.size()));
        if (len == 0) return {};
        if (len < buffer.size() - 1) return std::wstring(buffer.data(), len);
        if (buffer.size() >= 32768) return {};
        buffer.resize(buffer.size() * 2);
    }
}

bool ReadRegString(HKEY root, const std::wstring& subKey, const wchar_t* valueName,
                   std::wstring& out) {
    out.clear();
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    DWORD type = 0;
    DWORD size = 0;
    LSTATUS status = RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &size);
    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) ||
        size < sizeof(wchar_t)) {
        RegCloseKey(key);
        return false;
    }
    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1, L'\0');
    status = RegQueryValueExW(key, valueName, nullptr, &type,
                              reinterpret_cast<LPBYTE>(buffer.data()), &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) return false;
    out.assign(buffer.data());
    return true;
}

// Registra CLSID + InprocServer32 y asocia el manejador a cada extensión.
//
// POLÍTICA DE ASOCIACIÓN (importante):
//   · En SystemFileAssociations se escribe SIEMPRE (capa aditiva: no sustituye
//     el manejador predeterminado del usuario).
//   · A nivel de extensión (HKCR\.ext\ShellEx\...) solo se escribe si NO hay
//     ya un manejador: así los formatos que Windows ya miniaturiza (JPG, PNG,
//     HEIC…) conservan su proveedor nativo y se gana la miniatura en los que
//     no tienen (PSD, TGA, PNM, PIC, RAW sin códec…).
// 'threadingModel' no es cosmético: el proveedor de miniaturas es sin estado y
// COM puede llamarlo desde hilos MTA del Explorador ("Both"), mientras que el
// manejador de vista previa crea ventanas y DEBE vivir en un apartamento STA
// ("Apartment"); declarar "Both" allí rompería la afinidad de hilo de Win32.
bool RegisterHandler(HKEY root, const wchar_t* clsidText, const wchar_t* friendly,
                     const wchar_t* handlerTypeKey, bool previewAppId,
                     const wchar_t* threadingModel) {
    const std::wstring dllPath = ModulePath();
    if (dllPath.empty()) return false;

    const std::wstring base = std::wstring(L"Software\\Classes\\CLSID\\") + clsidText;
    bool ok = true;
    ok = ok && SetRegString(root, base, nullptr, friendly);
    ok = ok && SetRegString(root, base + L"\\InprocServer32", nullptr, dllPath);
    ok = ok && SetRegString(root, base + L"\\InprocServer32", L"ThreadingModel",
                            threadingModel);
    if (previewAppId) {
        // Fuera de proceso a través de PrevHost (modelo recomendado).
        ok = ok && SetRegString(root, base + L"\\AppID", nullptr, kPrevHostAppId);
    }

    for (size_t i = 0; i < kShellExtensionCount; ++i) {
        const std::wstring ext = kShellExtensions[i];
        const std::wstring additive =
            L"Software\\Classes\\SystemFileAssociations\\" + ext + L"\\ShellEx\\" + handlerTypeKey;
        ok = ok && SetRegString(root, additive, nullptr, clsidText);

        const std::wstring direct =
            std::wstring(L"Software\\Classes\\") + ext + L"\\ShellEx\\" + handlerTypeKey;
        std::wstring existing;
        if (!ReadRegString(root, direct, nullptr, existing) || existing.empty()) {
            ok = ok && SetRegString(root, direct, nullptr, clsidText);
        }
    }
    return ok;
}

// Borra una asociación SOLO si sigue apuntando a nuestro CLSID: si otro visor
// la reemplazó, su elección se respeta al desinstalar.
void RemoveAssociationIfOurs(HKEY root, const std::wstring& subKey, const wchar_t* clsidText) {
    std::wstring current;
    if (!ReadRegString(root, subKey, nullptr, current)) return;
    if (lstrcmpiW(current.c_str(), clsidText) != 0) return;
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey.c_str(), 0, KEY_READ | KEY_WRITE, &key) == ERROR_SUCCESS) {
        RegDeleteValueW(key, nullptr);
        RegCloseKey(key);
    }
}

bool UnregisterHandler(HKEY root, const wchar_t* clsidText, const wchar_t* handlerTypeKey) {
    RegDeleteTreeW(root, (std::wstring(L"Software\\Classes\\CLSID\\") + clsidText).c_str());
    for (size_t i = 0; i < kShellExtensionCount; ++i) {
        const std::wstring ext = kShellExtensions[i];
        RemoveAssociationIfOurs(root,
            L"Software\\Classes\\SystemFileAssociations\\" + ext + L"\\ShellEx\\" + handlerTypeKey,
            clsidText);
        RemoveAssociationIfOurs(root,
            std::wstring(L"Software\\Classes\\") + ext + L"\\ShellEx\\" + handlerTypeKey,
            clsidText);
    }
    return true;
}

HRESULT RegisterShellForRoot(HKEY root) {
    if (!root) return E_INVALIDARG;
    const bool thumb = RegisterHandler(root, kThumbClsidText, kThumbFriendly,
                                       kThumbHandlerKey, false, L"Both");
    const bool preview = RegisterHandler(root, kPreviewClsidText, kPreviewFriendly,
                                         kPreviewHandlerKey, true, L"Apartment");
    return (thumb && preview) ? S_OK : HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
}

HRESULT UnregisterShellForRoot(HKEY root) {
    if (!root) return E_INVALIDARG;
    UnregisterHandler(root, kThumbClsidText, kThumbHandlerKey);
    UnregisterHandler(root, kPreviewClsidText, kPreviewHandlerKey);
    return S_OK;
}

}  // namespace

// ===========================================================================
// Exports del servidor COM
// ===========================================================================
extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (rclsid != CLSID_ArtpicstThumbnail && rclsid != CLSID_ArtpicstPreview) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    auto* factory = new (std::nothrow) ClassFactory(rclsid);
    if (!factory) return E_OUTOFMEMORY;
    const HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

extern "C" HRESULT WINAPI DllCanUnloadNow() {
    if (g_objectCount == 0 && g_serverLocks == 0) return S_OK;
    return S_FALSE;
}

// Registro manual (regsvr32, requiere elevación porque escribe en HKLM).
extern "C" HRESULT WINAPI DllRegisterServer() {
    return RegisterShellForRoot(HKEY_LOCAL_MACHINE);
}

extern "C" HRESULT WINAPI DllUnregisterServer() {
    return UnregisterShellForRoot(HKEY_LOCAL_MACHINE);
}

// Registro que consume el instalador (HKLM o HKCU según la elevación real).
extern "C" HRESULT WINAPI ArtpicstRegisterShell(HKEY root) {
    return RegisterShellForRoot(root);
}

extern "C" HRESULT WINAPI ArtpicstUnregisterShell(HKEY root) {
    return UnregisterShellForRoot(root);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInstance = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
