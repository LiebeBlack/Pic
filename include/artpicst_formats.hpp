// Lista ÚNICA de formatos de imagen que entiende ARTPICST.
//
// Antes existían tres listas independientes que ya habían divergido: el
// instalador asociaba 52 extensiones, la extensión de shell 50 (sin EMF/WMF,
// que WIC no rasteriza) y el `--register` del visor solo 13. Cualquier formato
// añadido en un sitio quedaba fuera en los otros dos. Ahora los tres incluyen
// este header y recorren esta tabla, así que un formato nuevo se propaga a
// asociaciones, "Abrir con", escaneo de carpetas, miniaturas y vista previa.
//
// `shellThumbnail` indica si el proveedor de miniaturas del Explorador
// (artpicst_shell.dll, basado en WIC/stb) puede rasterizar el formato. Los
// metarchivos vectoriales EMF/WMF se abren con el visor (GDI+), pero no se
// anuncian como miniaturas para no ofrecer una vista que fallaría.
#pragma once

#include <cstddef>

namespace artpicst {

struct ImageFormat {
    const wchar_t* extension;  // con punto, siempre en minúsculas
    bool shellThumbnail;       // admite miniatura/vista previa en el Explorador
};

inline constexpr ImageFormat kImageFormats[] = {
    // JPEG y familia
    { L".jpg",  true }, { L".jpeg", true }, { L".jpe",  true },
    { L".jfif", true }, { L".jif",  true }, { L".exif", true },

    // Formatos sin pérdida / web
    { L".png",  true }, { L".apng", true }, { L".gif",  true },
    { L".webp", true }, { L".avif", true }, { L".jxl",  true },

    // Mapa de bits y contenedores de Windows
    { L".bmp",  true }, { L".dib",  true }, { L".rle",  true },
    { L".ico",  true }, { L".cur",  true }, { L".icon", true },

    // TIFF, JPEG 2000, JPEG XR y HEIF
    { L".tif",  true }, { L".tiff", true }, { L".jp2",  true },
    { L".j2k",  true }, { L".jpx",  true }, { L".jxr",  true },
    { L".wdp",  true }, { L".hdp",  true }, { L".heic", true },
    { L".heif", true }, { L".hif",  true },

    // Formatos de autor / HDR / mapa de bits portátil
    { L".tga",  true }, { L".tpic", true }, { L".psd",  true },
    { L".hdr",  true }, { L".pic",  true }, { L".pnm",  true },
    { L".ppm",  true }, { L".pgm",  true }, { L".pbm",  true },

    // RAW de cámara (requiere el códec RAW de Windows para decodificar)
    { L".dng",  true }, { L".cr2",  true }, { L".cr3",  true },
    { L".nef",  true }, { L".arw",  true }, { L".orf",  true },
    { L".rw2",  true }, { L".raf",  true }, { L".sr2",  true },
    { L".kdc",  true }, { L".raw",  true },

    // Metarchivos vectoriales: el visor los abre con GDI+, pero no hay
    // miniatura porque no son rasterizables por WIC/stb.
    { L".emf",  false }, { L".wmf", false },
};

inline constexpr std::size_t kImageFormatCount =
    sizeof(kImageFormats) / sizeof(kImageFormats[0]);

}  // namespace artpicst
