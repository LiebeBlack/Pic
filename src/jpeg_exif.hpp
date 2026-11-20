// ============================================================================
// jpeg_exif.hpp — Lectura de orientación EXIF en JPEG (módulo testeable)
// ============================================================================
// Extraído de src/main.cpp para que los tests de regresión de seguridad
// compile y ejecute EXACTAMENTE el mismo código que el visor. Solo cabecera,
// sin dependencias de Windows: C stdio + <cstdint>/<vector>.
//
// INCLUYE el fix de seguridad de la auditoría: el offset del IFD se compara
// en 64 bits contra el tamaño del bloque (en aritmética uint32_t, un offset
// corrupto cercano a 0xFFFFFFFF desbordaba y PASABA el chequeo, leyendo fuera
// del buffer).
//
// Semántica de la API (idéntica a la original):
//   · Devuelve 1..8 según la etiqueta 0x0112 (orientation) del IFD 0.
//   · Cualquier fallo (archivo ausente, no-JPEG, EXIF ausente/corrupto,
//     offset fuera de rango) devuelve 1 = "normal", nunca un valor > 8.
//   · Solo escanea marcadores APPn: NO entra en el scan comprimido (0xDA).
// ============================================================================

#ifndef ARTPICST_JPEG_EXIF_HPP
#define ARTPICST_JPEG_EXIF_HPP

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

// _wfopen_s: declarada por <cstdio>/<stdio.h> en las DOS toolchains soportadas
// (MSVC de serie; MinGW-w64 en sus cabeceras stdio.h). El propio main.cpp ya
// la usa con MinGW sin fallback.

namespace artpicst {
namespace exif {

// Representación independiente de la API del renderizador para las ocho
// orientaciones EXIF. Compartida por JPEG (stb), WIC y los tests.
struct ImageTransform {
    int rotation = 0;
    bool flipH = false;
    bool flipV = false;
};

// Descompone las orientaciones 1..8 en una rotación horaria y espejos.
// Los valores ausentes, negativos o fuera del rango se tratan como normal.
//
// CONVENCIÓN DE COMPOSICIÓN: el consumidor aplica PRIMERO los espejos y DESPUÉS
// la rotación (así lo hacen TransformBgra y la matriz de visualización de
// main.cpp). EXIF describe 5 y 7 justo así («espejo horizontal y giro 270» / «y
// giro 90»), de modo que esos son los ángulos que van aquí. Con la convención
// contraria (rotar y luego voltear, la de GDI+ RotateFlipType) habría que
// intercambiar 5 y 7 — por eso main.cpp mantiene su propia tabla para la ruta
// de respaldo GDI+, que sí rota antes de voltear.
inline constexpr ImageTransform TransformFromOrientation(int orientation) noexcept {
    switch (orientation) {
        case 2: return {0, true, false};      // espejo horizontal
        case 3: return {180, false, false};   // giro 180
        case 4: return {0, false, true};      // espejo vertical
        case 5: return {270, true, false};    // espejo H + giro 270 = transpuesta
        case 6: return {90, false, false};    // giro 90
        case 7: return {90, true, false};     // espejo H + giro 90 = antitranspuesta
        case 8: return {270, false, false};   // giro 270
        default: return {};
    }
}

// Lee la orientación EXIF (1..8) de un fichero JPEG. Siempre devuelve un
// valor seguro: 1 ante cualquier duda.
inline int ReadJpegOrientation(const wchar_t* filepath) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, filepath, L"rb") != 0 || !f) return 1;

    unsigned char header[2];
    if (fread(header, 1, 2, f) != 2 || header[0] != 0xFF || header[1] != 0xD8) {
        fclose(f);
        return 1;
    }

    int orientation = 1;
    bool foundExif = false;
    while (true) {
        unsigned char marker[2];
        if (fread(marker, 1, 2, f) != 2 || marker[0] != 0xFF) break;
        if (marker[1] == 0xDA || marker[1] == 0xD9) break;

        unsigned char lenBytes[2];
        if (fread(lenBytes, 1, 2, f) != 2) break;
        int len = (lenBytes[0] << 8) | lenBytes[1];
        if (len < 2) break;

        if (marker[1] == 0xE1 && len >= 14) {
            std::vector<unsigned char> data(static_cast<size_t>(len) - 2);
            const bool readOk = fread(data.data(), 1, len - 2, f) == static_cast<size_t>(len - 2);
            if (readOk) {
                if (memcmp(data.data(), "Exif\0\0", 6) == 0) {
                    foundExif = true;
                    const unsigned char* tiff = data.data() + 6;
                    const size_t tiffLen = data.size() - 6;
                    if (tiffLen >= 8) {
                        const bool littleEndian = (tiff[0] == 'I' && tiff[1] == 'I');
                        auto read16 = [littleEndian](const unsigned char* p) -> uint16_t {
                            return littleEndian ? static_cast<uint16_t>(p[0] | (p[1] << 8))
                                                : static_cast<uint16_t>((p[0] << 8) | p[1]);
                        };
                        auto read32 = [littleEndian](const unsigned char* p) -> uint32_t {
                            return littleEndian
                                ? (static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                                   (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24))
                                : ((static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
                                   (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]));
                        };
                        const uint32_t ifdOffset = read32(tiff + 4);
                        // FIX de seguridad: comparar en 64 bits. En aritmética
                        // uint32_t, un offset corrupto cerca de 0xFFFFFFFF
                        // desborda (0xFFFFFFFF+2 == 1) y PASABA el chequeo,
                        // leyendo fuera del buffer con un JPEG manipulado.
                        if (static_cast<size_t>(ifdOffset) + 2 <= tiffLen) {
                            const uint16_t tagCount = read16(tiff + ifdOffset);
                            const unsigned char* tagPtr = tiff + ifdOffset + 2;
                            // tagPtr + 12 <= tiff + tiffLen: idéntico guard de
                            // límites por etiqueta que en el original.
                            for (uint16_t i = 0; i < tagCount && (tagPtr + 12 <= tiff + tiffLen); ++i, tagPtr += 12) {
                                const uint16_t tag = read16(tagPtr);
                                if (tag == 0x0112) {
                                    const int o = read16(tagPtr + 8);
                                    orientation = (o >= 1 && o <= 8) ? o : 1;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            // Un APP1 que NO es EXIF (XMP, IPTC, ...) no es motivo para dejar de
            // buscar: el bloque EXIF puede venir en un APP1 POSTERIOR. Antes se
            // abandonaba aquí y esas fotos se mostraban sin orientar. Solo se
            // sale si ya hay EXIF o si el segmento no se pudo leer completo.
            if (foundExif || !readOk) break;
            continue;
        } else {
            fseek(f, static_cast<long>(len) - 2, SEEK_CUR);
        }
    }
    fclose(f);
    return orientation;
}

} // namespace exif
} // namespace artpicst

#endif  // ARTPICST_JPEG_EXIF_HPP
