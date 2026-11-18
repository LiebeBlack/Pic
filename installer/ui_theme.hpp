// ============================================================================
// ui_theme.hpp — Tema visual compartido "Aurora Carbon"
// ----------------------------------------------------------------------------
// Paleta y primitivas de dibujo GDI+ comunes al INSTALADOR y al ACTUALIZADOR,
// para que ambos binarios compartan identidad visual y el código de dibujo
// viva en un único punto (antes estaba duplicado en cada .cpp).
//
//   · Fondo grafito profundo (#0B0E11) con resplandores "aurora" suaves.
//   · Acento degradado menta -> cielo (#20E3B2 -> #2E9BFF).
//   · Botones "pill" (radio = alto/2), tarjetas con radio amplio y
//     bordes de un píxel de bajo contraste.
//
// Solo tipografías y tamaños de fuente quedan en cada .cpp (sus "Fonts"
// locales); todo lo demás se comparte desde aquí.
//
// NO tocar sin revisar los DOS binarios: installer/artpicst_installer.cpp y
// updater/artpicst_updater.cpp incluyen esta cabecera.
// ============================================================================
#pragma once

#include <gdiplus.h>
#include <cmath>

namespace artpicst {
namespace ui {

// ============================================================================
// Paleta "Aurora Carbon"
// ============================================================================
inline const Gdiplus::Color COL_BG(255, 11, 14, 17);            // #0B0E11 fondo principal
inline const Gdiplus::Color COL_PANEL(255, 18, 22, 27);         // #12161B paneles / encabezado
inline const Gdiplus::Color COL_PANEL_DEEP(255, 14, 18, 22);    // paneles hundidos (log, licencia)
inline const Gdiplus::Color COL_PANEL_BORDER(255, 35, 43, 51);  // #232B33 bordes hairline
inline const Gdiplus::Color COL_ACCENT_A(255, 32, 227, 178);    // #20E3B2 menta (inicio degradado)
inline const Gdiplus::Color COL_ACCENT_B(255, 46, 155, 255);    // #2E9BFF cielo (fin degradado)
inline const Gdiplus::Color COL_TEXT(255, 236, 242, 248);       // #ECF2F8 texto principal
inline const Gdiplus::Color COL_TEXT_SOFT(255, 159, 176, 190);  // #9FB0BE texto secundario
inline const Gdiplus::Color COL_TEXT_DIM(255, 100, 116, 129);   // #647481 texto terciario
inline const Gdiplus::Color COL_BTN_GHOST(255, 22, 27, 33);     // #161B21 botón fantasma
inline const Gdiplus::Color COL_BTN_GHOST_HOT(255, 30, 37, 45); // #1E252D botón fantasma hover
inline const Gdiplus::Color COL_BTN_GHOST_BORDER(255, 42, 51, 61);
inline const Gdiplus::Color COL_BTN_GHOST_BORDER_HOT(255, 32, 227, 178);   // borde hover = acento
inline const Gdiplus::Color COL_SUCCESS(255, 47, 227, 155);     // #2FE39B
inline const Gdiplus::Color COL_ERROR(255, 255, 107, 107);      // #FF6B6B
inline const Gdiplus::Color COL_WARN(255, 255, 194, 75);        // #FFC24B
inline const Gdiplus::Color COL_DISABLED_TEXT(255, 92, 104, 116);

// ============================================================================
// Primitivas de dibujo (unidades de diseño 96 DPI)
// ============================================================================

// Traza un rectángulo redondeado en 'path' (radio acotado a la mitad del lado
// menor: nunca produce arcos degenerados).
inline void RoundPath(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rc, float radius) {
    const float w = rc.Width;
    const float h = rc.Height;
    float rad = radius;
    if (rad < 0.0f) rad = 0.0f;
    const float maxRad = (w < h ? w : h) * 0.5f;
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

inline void FillRound(Gdiplus::Graphics& g, const Gdiplus::RectF& rc, float radius,
                      const Gdiplus::Color& fill) {
    Gdiplus::GraphicsPath path;
    RoundPath(path, rc, radius);
    Gdiplus::SolidBrush brush(fill);
    g.FillPath(&brush, &path);
}

inline void FillRoundGradient(Gdiplus::Graphics& g, const Gdiplus::RectF& rc, float radius,
                              const Gdiplus::Color& c1, const Gdiplus::Color& c2,
                              bool vertical = false) {
    Gdiplus::GraphicsPath path;
    RoundPath(path, rc, radius);
    Gdiplus::LinearGradientBrush brush(rc, c1, c2, vertical ? 90.0f : 0.0f);
    g.FillPath(&brush, &path);
}

inline void StrokeRound(Gdiplus::Graphics& g, const Gdiplus::RectF& rc, float radius,
                        const Gdiplus::Color& border, float width = 1.0f) {
    Gdiplus::GraphicsPath path;
    RoundPath(path, rc, radius);
    Gdiplus::Pen pen(border, width);
    g.DrawPath(&pen, &path);
}

inline void DrawTextIn(Gdiplus::Graphics& g, const wchar_t* text, const Gdiplus::RectF& rc,
                       const Gdiplus::Font& font, const Gdiplus::Color& color,
                       bool centerH = true, bool centerV = true,
                       Gdiplus::StringTrimming trimming = Gdiplus::StringTrimmingNone,
                       bool noWrap = false) {
    Gdiplus::StringFormat format;
    format.SetAlignment(centerH ? Gdiplus::StringAlignmentCenter : Gdiplus::StringAlignmentNear);
    format.SetLineAlignment(centerV ? Gdiplus::StringAlignmentCenter : Gdiplus::StringAlignmentNear);
    if (trimming != Gdiplus::StringTrimmingNone) {
        format.SetTrimming(trimming);
        if (noWrap) format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    }
    Gdiplus::SolidBrush brush(color);
    g.DrawString(text, -1, &font, rc, &format, &brush);
}

// Marca de verificación de dos trazos con extremos redondeados.
inline void DrawCheckMark(Gdiplus::Graphics& g, float x0, float y0, float x1, float y1,
                          float x2, float y2, float width, const Gdiplus::Color& color) {
    Gdiplus::Pen pen(color, width);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    g.DrawLine(&pen, x0, y0, x1, y1);
    g.DrawLine(&pen, x1, y1, x2, y2);
}

// Casilla de verificación (18 px habitual): relleno degradado + check blanco
// cuando está marcada; pozo con borde hairline cuando no.
inline void DrawCheckBox(Gdiplus::Graphics& g, const Gdiplus::RectF& rc, bool checked) {
    const float radius = rc.Height * 0.28f;
    if (checked) {
        FillRoundGradient(g, rc, radius, COL_ACCENT_A, COL_ACCENT_B);
        DrawCheckMark(g, rc.X + rc.Width * 0.22f, rc.Y + rc.Height * 0.52f,
                      rc.X + rc.Width * 0.42f, rc.Y + rc.Height * 0.72f,
                      rc.X + rc.Width * 0.80f, rc.Y + rc.Height * 0.28f,
                      2.0f, Gdiplus::Color(255, 255, 255, 255));
    } else {
        FillRound(g, rc, radius, Gdiplus::Color(255, 12, 15, 21));
        StrokeRound(g, rc, radius, Gdiplus::Color(255, 58, 68, 86), 1.0f);
    }
}

// Logotipo de la marca: tesela squircle con degradado menta->cielo, brillo
// superior y la letra "A" blanca. 'glyph' aporta la tipografía (su tamaño se
// adapta al cuadro en cada binario).
inline void DrawLogoMark(Gdiplus::Graphics& g, const Gdiplus::Font& glyph,
                         float cx, float cy, float size) {
    const Gdiplus::RectF tile(cx - size * 0.5f, cy - size * 0.5f, size, size);
    // Halo translúcido detrás de la tesela.
    const Gdiplus::RectF halo(tile.X - size * 0.16f, tile.Y - size * 0.16f,
                              size * 1.32f, size * 1.32f);
    FillRound(g, halo, size * 0.40f, Gdiplus::Color(34, 32, 227, 178));
    FillRoundGradient(g, tile, size * 0.30f, COL_ACCENT_A, COL_ACCENT_B);
    // Brillo superior tipo "cristal".
    const Gdiplus::RectF shine(tile.X + size * 0.09f, tile.Y + size * 0.09f,
                               size * 0.82f, size * 0.32f);
    FillRound(g, shine, size * 0.16f, Gdiplus::Color(42, 255, 255, 255));
    DrawTextIn(g, L"A", tile, glyph, Gdiplus::Color(255, 255, 255, 255), true, true);
}

// ============================================================================
// Suavizado de progreso independiente del tiempo
// ============================================================================
// Interpolación exponencial con constante de tiempo: el resultado es idéntico
// a 30, 60 o 144 Hz (no depende de cuántas veces se ejecute el temporizador) y
// aterriza exacto en el objetivo, sin el "temblor" final del factor fijo.
// 'tau' es la constante de tiempo en segundos (menor = más rápido).
inline double SmoothTowards(double current, double target, double dtSeconds,
                            double tauSeconds = 0.09, double snap = 0.05) {
    if (dtSeconds <= 0.0) return current;
    if (tauSeconds <= 0.0) return target;
    const double k = 1.0 - std::exp(-dtSeconds / tauSeconds);
    double next = current + (target - current) * k;
    if (std::fabs(target - next) <= snap) next = target;
    // Nunca retrocede ni adelanta el objetivo (evita rebotes al cambiar de
    // fase con objetivos decrecientes).
    if (target >= current) { if (next > target) next = target; }
    else                   { if (next < target) next = target; }
    return next;
}

// ============================================================================
// Back buffer persistente (anti-parpadeo y coste O(1) por fotograma)
// ============================================================================
// Instalador y actualizador componían cada frame en un HDC de memoria que se
// creaba y destruía en CADA WM_PAINT (hasta 60 veces por segundo): GDI
// asignaba y liberaba varios MB/s y el asignador del sistema acababa marcando
// el ritmo del repintado. Este búfer se reutiliza y solo se recrea cuando
// cambia el tamaño del cliente, de modo que pintar pasa a ser una copia.
class BackBuffer {
public:
    BackBuffer() = default;
    ~BackBuffer() { Release(); }
    BackBuffer(const BackBuffer&) = delete;
    BackBuffer& operator=(const BackBuffer&) = delete;

    // Garantiza un búfer del tamaño pedido. false => el llamador pinta directo.
    bool Ensure(HDC reference, int width, int height) {
        if (!reference || width <= 0 || height <= 0) return false;
        if (dc_ && width == width_ && height == height_) return true;
        Release();
        dc_ = CreateCompatibleDC(reference);
        if (!dc_) return false;
        bmp_ = CreateCompatibleBitmap(reference, width, height);
        if (!bmp_) {
            DeleteDC(dc_);
            dc_ = nullptr;
            return false;
        }
        old_ = SelectObject(dc_, bmp_);
        width_ = width;
        height_ = height;
        return true;
    }

    void Release() {
        if (dc_) {
            if (old_) SelectObject(dc_, old_);
            if (bmp_) DeleteObject(bmp_);
            DeleteDC(dc_);
        }
        dc_ = nullptr;
        bmp_ = nullptr;
        old_ = nullptr;
        width_ = 0;
        height_ = 0;
    }

    HDC Dc() const { return dc_; }

    void BlitTo(HDC target) const {
        if (dc_ && target) BitBlt(target, 0, 0, width_, height_, dc_, 0, 0, SRCCOPY);
    }

private:
    HDC     dc_ = nullptr;
    HBITMAP bmp_ = nullptr;
    HGDIOBJ old_ = nullptr;
    int     width_ = 0;
    int     height_ = 0;
};

}  // namespace ui
}  // namespace artpicst
