// ============================================================================
// jpeg_exif_test.cpp — Tests de regresión del lector de orientación EXIF
// ============================================================================
// Ejecuta EXACTAMENTE el mismo código que usa el visor (src/jpeg_exif.hpp)
// contra JPEGs sintéticos construidos en memoria y volcados a ficheros
// temporales. Cubre:
//   · JPEG válidos con orientación 1..8 (little y big endian TIFF).
//   · JPEG corruptos de la auditoría: offset del IFD con desbordamiento
//     uint32_t (0xFFFFFFFE/0xFFFFFFFF), tagCount gigante, IFD fuera de rango,
//     EXIF truncado.
//   · No-JPEG: vacío, cabecera errónea, truncado a mitad de markers.
//   · DECISIÓN DISEÑO (no sobrecorregir): un JPEG sin EXIF devuelve 1, no 0,
//     porque 0 nunca fue un valor posible de la API y "1 = normal" es la
//     semántica que el pipeline de rotación ya espera. Los tests fijan esa
//     decisión como contrato.
//
// Compilación (MSVC):
//   cl /nologo /EHsc /std:c++20 /O2 /utf-8 /W4 /permissive- /I. ^
//      /Fe:build\jpeg_exif_test.exe tests\jpeg_exif_test.cpp /link kernel32.lib shell32.lib
// Compilación (MinGW-w64):
//   g++ -std=c++20 -O2 -Wall -Wextra -I. ^
//      -o build/jpeg_exif_test.exe tests/jpeg_exif_test.cpp
// ============================================================================

#include "../src/jpeg_exif.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#ifndef _WINDOWS_
extern "C" __declspec(dllimport) unsigned long __stdcall GetEnvironmentVariableW(
    const wchar_t*, wchar_t*, unsigned long);
extern "C" unsigned long __stdcall GetCurrentDirectoryW(
    unsigned long, wchar_t*);
#endif

namespace {

int g_failures = 0;

#define CHECK_MSG(cond, msg)                                               \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::wprintf(L"[FAIL] %hs: %hs\n", msg, #cond);                \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

// ----------------------------------------------------------------------------
// Construcción sintética de JPEG
// ----------------------------------------------------------------------------

// SOI + APP1(Exif) con un TIFF embebido construido a mano.
std::vector<unsigned char> MakeJpegWithExif(uint32_t ifdOffsetRaw,
                                            uint16_t tagCount,
                                            bool includeOrientationTag = true,
                                            int orientationValue = 6,
                                            bool littleEndian = true,
                                            bool truncateApp1 = false) {
    // Cabecera TIFF: byte order + 0x002A + offset del IFD (4 bytes).
    std::vector<unsigned char> tiff;
    if (littleEndian) {
        tiff = {'I', 'I', 0x2A, 0x00};
    } else {
        tiff = {'M', 'M', 0x00, 0x2A};
    }
    auto push16 = [&tiff](uint16_t v, bool le) {
        if (le) { tiff.push_back(static_cast<unsigned char>(v & 0xFF)); tiff.push_back(static_cast<unsigned char>(v >> 8)); }
        else    { tiff.push_back(static_cast<unsigned char>(v >> 8));   tiff.push_back(static_cast<unsigned char>(v & 0xFF)); }
    };
    auto push32 = [&tiff](uint32_t v, bool le) {
        if (le) {
            tiff.push_back(static_cast<unsigned char>(v & 0xFF));
            tiff.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
            tiff.push_back(static_cast<unsigned char>((v >> 16) & 0xFF));
            tiff.push_back(static_cast<unsigned char>((v >> 24) & 0xFF));
        } else {
            tiff.push_back(static_cast<unsigned char>((v >> 24) & 0xFF));
            tiff.push_back(static_cast<unsigned char>((v >> 16) & 0xFF));
            tiff.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
            tiff.push_back(static_cast<unsigned char>(v & 0xFF));
        }
    };
    push32(tiff, ifdOffsetRaw, littleEndian);

    // IFD en el offset 8: tagCount + entradas (tag 0x0112) + next-IFD 0.
    // Cada entrada: tag(2) type(2) count(4) value/offset(4) = 12 bytes.
    std::vector<unsigned char> ifd;
    push16(ifd, tagCount, littleEndian);
    if (includeOrientationTag && tagCount > 0) {
        push16(ifd, 0x0112, littleEndian);            // tag Orientation
        push16(ifd, 3, littleEndian);                 // type SHORT
        push32(ifd, 1, littleEndian);                 // count
        push16(ifd, static_cast<uint16_t>(orientationValue), littleEndian); // value (SHORT ocupa los 2 primeros bytes)
        push16(ifd, 0, littleEndian);                 // padding
    }
    push32(ifd, 0, littleEndian);                     // next IFD = 0

    // El offset del IFD real debe apuntar a la posición de ifd dentro de tiff
    // (8), salvo en los casos corruptos donde ifdOffsetRaw es deliberadamente
    // gigante y el IFD sigue estando físico en 8 para que tagCount sea legible.
    tiff.insert(tiff.end(), ifd.begin(), ifd.end());

    // Envoltura Exif: "Exif\0\0" + TIFF.
    std::vector<unsigned char> app1Payload;
    const char exifId[6] = {'E', 'x', 'i', 'f', '\0', '\0'};
    app1Payload.insert(app1Payload.end(), exifId, exifId + 6);
    app1Payload.insert(app1Payload.end(), tiff.begin(), tiff.end());

    // Marker APP1 con longitud big-endian (incluye los propios 2 bytes).
    std::vector<unsigned char> jpeg = {0xFF, 0xD8};   // SOI
    if (!truncateApp1) {
        const uint16_t segLen = static_cast<uint16_t>(app1Payload.size() + 2);
        jpeg.push_back(0xFF); jpeg.push_back(0xE1);
        jpeg.push_back(static_cast<unsigned char>(segLen >> 8));
        jpeg.push_back(static_cast<unsigned char>(segLen & 0xFF));
    } else {
        jpeg.push_back(0xFF); jpeg.push_back(0xE1);
        // Longitud que promete más bytes de los que vienen: EXIF truncado.
        jpeg.push_back(0xFF); jpeg.push_back(0xFF);
    }
    jpeg.insert(jpeg.end(), app1Payload.begin(), app1Payload.end());
    // EOI para que el fichero sea un JPEG mínimamente válido.
    jpeg.push_back(0xFF); jpeg.push_back(0xD9);
    return jpeg;
}

constexpr int kPathMax = 512;   // suficiente para rutas temporales del test

// Escribe a un fichero temporal y devuelve la ruta.
std::wstring WriteTempJpeg(const std::vector<unsigned char>& bytes, const wchar_t* tag) {
    static int counter = 0;
    wchar_t path[kPathMax];
    wchar_t dir[480] = {};
    GetCurrentDirectoryW(479, dir);
    // %ls (no %s): el significado de %s en funciones anchas difiere entre
    // MSVC (wide) y MinGW con __USE_MINGW_ANSI_STDIO (narrow); %ls es wide
    // en ambas toolchains.
    swprintf(path, kPathMax, L"%ls\\artpicst_exif_test_%ls_%d.tmp", dir, tag, ++counter);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"wb") != 0 || !f) return L"";
    fwrite(bytes.data(), 1, bytes.size(), f);
    fclose(f);
    return path;
}

void DeleteTemp(const std::wstring& path) {
    if (!path.empty()) _wremove(path.c_str());
}

// ----------------------------------------------------------------------------
// Casos
// ----------------------------------------------------------------------------

void TestValidOrientations() {
    for (int expected = 1; expected <= 8; ++expected) {
        const auto jpeg = MakeJpegWithExif(8, 1, true, expected, true);
        const std::wstring path = WriteTempJpeg(jpeg, L"valid");
        CHECK_MSG(!path.empty(), "temp file creado");
        if (path.empty()) return;
        CHECK_MSG(artpicst::exif::ReadJpegOrientation(path.c_str()) == expected,
                  "orientación little-endian válida");
        DeleteTemp(path);
    }
    // Big endian (MM): orientación 8.
    {
        const auto jpeg = MakeJpegWithExif(8, 1, true, 8, false);
        const std::wstring path = WriteTempJpeg(jpeg, L"be");
        CHECK_MSG(artpicst::exif::ReadJpegOrientation(path.c_str()) == 8,
                  "orientación big-endian válida");
        DeleteTemp(path);
    }
    // Varias etiquetas antes de la Orientation: se sigue encontrando.
    {
        const auto jpeg = MakeJpegWithExif(8, 3, true, 6, true);
        const std::wstring path = WriteTempJpeg(jpeg, L"multi");
        // tagCount=3 pero solo 1 entrada física: el guard de límites corta el
        // recorrido sin leer fuera; la orientación (primera entrada) se lee.
        CHECK_MSG(artpicst::exif::ReadJpegOrientation(path.c_str()) == 6,
                  "orientación con tagCount mayor que entradas físicas");
        DeleteTemp(path);
    }
}

void TestCorruptOffsets() {
    // Los offsets corruptos de la auditoría: en aritmética uint32_t,
    // 0xFFFFFFFE + 2 == 0 (desborda) y 0xFFFFFFFF + 2 == 1 — pasaban el
    // chequeo y provocaban lectura fuera del buffer. Ahora deben devolver 1.
    const uint32_t evilOffsets[] = {
        0xFFFFFFFEu, 0xFFFFFFFFu, 0x80000000u, 0xFFFFFFF0u,
        0x00000000u,                      // IFD en el propio header: corto pero legible; sin tag -> 1
        0x7FFFFFFFu,                      // enorme pero no desbordante: fuera de rango
    };
    for (uint32_t off : evilOffsets) {
        const auto jpeg = MakeJpegWithExif(off, 1, true, 6, true);
        const std::wstring path = WriteTempJpeg(jpeg, L"evil");
        const int got = artpicst::exif::ReadJpegOrientation(path.c_str());
        if (got < 1 || got > 8) {
            std::wprintf(L"[FAIL] offset corrupto 0x%08X devolvió %d (fuera de 1..8)\n", off, got);
            ++g_failures;
        }
        DeleteTemp(path);
    }
    // tagCount gigante: el guard por etiqueta debe limitar el recorrido.
    {
        const auto jpeg = MakeJpegWithExif(8, 0xFFFF, true, 6, true);
        const std::wstring path = WriteTempJpeg(jpeg, L"tagcount");
        const int got = artpicst::exif::ReadJpegOrientation(path.c_str());
        CHECK_MSG(got >= 1 && got <= 8, "tagCount gigante acotado");
        DeleteTemp(path);
    }
    // EXIF truncado: longitud APP1 promete 65535 bytes pero el fichero acaba ahí.
    {
        const auto jpeg = MakeJpegWithExif(8, 1, true, 6, true, true);
        const std::wstring path = WriteTempJpeg(jpeg, L"trunc");
        const int got = artpicst::exif::ReadJpegOrientation(path.c_str());
        CHECK_MSG(got >= 1 && got <= 8, "EXIF truncado seguro");
        DeleteTemp(path);
    }
    // Bloque TIFF más corto que 8 bytes: sin crash.
    {
        std::vector<unsigned char> jpeg = {0xFF, 0xD8, 0xFF, 0xE1, 0x00, 0x0A,
                                           'E', 'x', 'i', 'f', '\0', '\0', 'I', 'I'};
        const std::wstring path = WriteTempJpeg(jpeg, L"shorttiff");
        CHECK_MSG(artpicst::exif::ReadJpegOrientation(path.c_str()) == 1,
                  "TIFF corto devuelve 1");
        DeleteTemp(path);
    }
    // Payload sin firma "Exif\0\0" dentro del APP1.
    {
        std::vector<unsigned char> jpeg = {0xFF, 0xD8, 0xFF, 0xE1, 0x00, 0x10,
                                           'X', 'P', 'I', '\0', '\0', '\0', 'I', 'I', 0x2A, 0x00,
                                           8, 0, 0, 0};
        const std::wstring path = WriteTempJpeg(jpeg, L"nofirma");
        CHECK_MSG(artpicst::exif::ReadJpegOrientation(path.c_str()) == 1,
                  "APP1 sin firma EXIF devuelve 1");
        DeleteTemp(path);
    }
}

void TestNotJpeg() {
    // Fichero vacío.
    {
        const std::wstring path = WriteTempJpeg({}, L"empty");
        CHECK_MSG(artpicst::exif::ReadJpegOrientation(path.c_str()) == 1, "vacío devuelve 1");
        DeleteTemp(path);
    }
    // Cabecera no-JPEG.
    {
        std::vector<unsigned char> notJpeg = {'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
        const std::wstring path = WriteTempJpeg(notJpeg, L"png");
        CHECK_MSG(artpicst::exif::ReadJpegOrientation(path.c_str()) == 1, "no-JPEG devuelve 1");
        DeleteTemp(path);
    }
    // Truncado: SOI + mitad de marker.
    {
        std::vector<unsigned char> cut = {0xFF, 0xD8, 0xFF};
        const std::wstring path = WriteTempJpeg(cut, L"cut");
        CHECK_MSG(artpicst::exif::ReadJpegOrientation(path.c_str()) == 1, "SOI+mitad devuelve 1");
        DeleteTemp(path);
    }
    // Longitud de segmento absurda (0 o 1): break limpio, devuelve 1.
    {
        std::vector<unsigned char> weird = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x01, 0xFF, 0xD9};
        const std::wstring path = WriteTempJpeg(weird, L"len1");
        CHECK_MSG(artpicst::exif::ReadJpegOrientation(path.c_str()) == 1, "longitud < 2 devuelve 1");
        DeleteTemp(path);
    }
    // Rutas inexistentes / vacías.
    CHECK_MSG(artpicst::exif::ReadJpegOrientation(L"") == 1, "ruta vacía devuelve 1");
    CHECK_MSG(artpicst::exif::ReadJpegOrientation(L"Z:\\no\\existe\\nunca.jpg") == 1,
              "ruta inexistente devuelve 1");
}

} // namespace

int main() {
    std::wprintf(L"== ARTPICST jpeg_exif tests ==\n");
    TestValidOrientations();
    TestCorruptOffsets();
    TestNotJpeg();
    if (g_failures == 0) {
        std::wprintf(L"[OK] jpeg_exif: todos los tests superados\n");
        return 0;
    }
    std::wprintf(L"[FAIL] jpeg_exif: %d fallos\n", g_failures);
    return 1;
}
