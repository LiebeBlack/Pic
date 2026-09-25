#!/usr/bin/env python3
"""Genera resources/artpicst.ico: icono multirresolucion SIN dependencias externas.

Por que existe este script
--------------------------
El icono antiguo del repositorio tenia 268 bytes y UNA sola imagen de 16x16: el
script anterior guardaba ``images[0]`` con una lista de tamanos, de modo que
Windows escalaba el mapa de 16 px al resto y el resultado eran miniaturas
borrosas. Aqui se dibuja la marca a CADA resolucion real (16 -> 256 px), de forma
que el Explorador siempre encuentra el tamano exacto que necesita.

Por que no usa Pillow
---------------------
``pillow`` no es una dependencia del proyecto y en muchos entornos (CI sin red,
Python "embebido", maquinas sin pip) no esta disponible: el .ico del repositorio
nunca se regeneraba. Este script usa solo la biblioteca estandar (``struct``,
``zlib``, ``math``) y rasteriza la marca con firmas de distancia (SDF) y
cobertura analitica, lo que ademas da bordes nitidos y suavizados sin
supersampling ni reescalado.

Uso:
    python scripts/generate_icon.py            # regenera resources/artpicst.ico
    python scripts/generate_icon.py --preview  # ademas escribe un PNG 256x256
"""

from __future__ import annotations

import argparse
import math
import struct
import sys
import zlib
from pathlib import Path

# Resoluciones que usa Windows: 16/20/24/32/40/48 para el shell y la barra de
# tareas, 64/96/128 para las vistas de iconos y 256 para "iconos extragrandes",
# el panel de vista previa y las escalas 150-200 %.
SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)

# Las imagenes >= 128 px se guardan como PNG dentro del .ico (lo admite Windows
# Vista y posteriores); el resto, como DIB de 32 bits con mascara AND, que es el
# formato que entienden TODOS los manejadores de miniaturas, incluidos los
# antiguos. Asi el .ico queda pequeno y 100 % compatible.
PNG_FROM = 128

# ---------------------------------------------------------------------------
# Paleta de la marca (misma familia visual que la interfaz e instalador)
# ---------------------------------------------------------------------------
ACCENT_TOP = (86, 160, 255)       # azul
ACCENT_BOTTOM = (58, 96, 240)     # azul-violeta
CARD = (17, 22, 33)               # panel oscuro
CARD_LIGHT = (32, 41, 58)         # brillo superior del panel
MOUNTAIN = (120, 190, 255)
MOUNTAIN_FAR = (76, 132, 214)
SUN = (255, 214, 132)
HORIZON = (14, 18, 27)

# Geometria de la marca, en coordenadas normalizadas (0..1) sobre el cuadrado
# completo. Los valores repiten la composicion del logotipo original pero
# expresados de forma parametrica, para que cualquier tamano sea consistente.
PAD = 0.14
INNER = 1.0 - PAD * 2.0
CARD_RADIUS = 0.22
PANEL_RADIUS = 0.13

PANEL_CX = 0.5
PANEL_CY = 0.5
PANEL_H = INNER * 0.5

SUN_CX = PAD + INNER * 0.70
SUN_CY = PAD + INNER * 0.30
SUN_R = INNER * 0.11

BASE_Y = PAD + INNER - INNER * 0.10
PEAK_Y = PAD + INNER * 0.34
LEFT_X = PAD + INNER * 0.06
MID_X = PAD + INNER * 0.46
RIGHT_X = PAD + INNER - INNER * 0.06

HORIZON_X0 = PAD + INNER * 0.05
HORIZON_X1 = PAD + INNER - INNER * 0.05
HORIZON_H = INNER * 0.04


# ---------------------------------------------------------------------------
# Firmas de distancia (SDF): negativo dentro, positivo fuera
# ---------------------------------------------------------------------------
def _sd_round_rect(px: float, py: float, cx: float, cy: float,
                   hx: float, hy: float, r: float) -> float:
    """Distancia al rectangulo [cx±hx, cy±hy] con esquinas de radio r."""
    qx = abs(px - cx) - (hx - r)
    qy = abs(py - cy) - (hy - r)
    outside = math.hypot(qx if qx > 0.0 else 0.0, qy if qy > 0.0 else 0.0)
    inside = min(max(qx, qy), 0.0)
    return outside + inside - r


def _sd_circle(px: float, py: float, cx: float, cy: float, r: float) -> float:
    return math.hypot(px - cx, py - cy) - r


def _edge_distances(px: float, py: float, tri):
    """Distancias con signo del punto a los tres lados dirigidos del triangulo."""
    out = []
    for (x0, y0), (x1, y1) in ((tri[0], tri[1]), (tri[1], tri[2]), (tri[2], tri[0])):
        ex, ey = x1 - x0, y1 - y0
        length = math.hypot(ex, ey)
        if length == 0.0:
            continue
        # Normal del lado (apunta a la izquierda del vector dirigido).
        out.append(((px - x0) * (-ey) + (py - y0) * ex) / length)
    return out


def _sd_triangle(px: float, py: float, tri) -> float:
    """Distancia con signo a un triangulo (negativa dentro).

    Para un poligono convexo el interior queda del MISMO lado de los tres lados,
    asi que la distancia exterior es ``max(d)`` o ``-min(d)`` segun el sentido de
    giro de los vertices. El sentido se deduce evaluando el centroide (que siempre
    esta dentro) y no se asume ninguna convencion de winding."""
    ds = _edge_distances(px, py, tri)
    if not ds:
        return 1.0                       # triangulo degenerado: nada que pintar
    gx = (tri[0][0] + tri[1][0] + tri[2][0]) / 3.0
    gy = (tri[0][1] + tri[1][1] + tri[2][1]) / 3.0
    centro = _edge_distances(gx, gy, tri)
    if max(centro) < 0.0:                # interior = lado negativo
        return max(ds)
    return -min(ds)                      # interior = lado positivo


def _coverage(sd: float, size: int, softness: float = 1.15) -> float:
    """Cobertura 0..1 de un pixel a partir de la distancia en unidades normalizadas.

    ``sd * size`` es la distancia en pixeles; se reparte en un ancho de
    ``softness`` px, lo que da antialiasing real sin supersampling."""
    t = 0.5 - (sd * size) / softness
    if t <= 0.0:
        return 0.0
    if t >= 1.0:
        return 1.0
    return t


def _lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


# ---------------------------------------------------------------------------
# Rasterizado
# ---------------------------------------------------------------------------
def render(size: int) -> bytes:
    """Devuelve los pixeles RGBA (no premultiplicados) de la marca a ``size`` px."""
    inv = 1.0 / size
    buf = bytearray(size * size * 4)
    panel = (PANEL_CX, PANEL_CY, PANEL_H, PANEL_H, PANEL_RADIUS)
    highlight_bottom = PAD + INNER * 0.45
    tri_near = ((LEFT_X, BASE_Y), (MID_X, PEAK_Y), (RIGHT_X, BASE_Y))
    tri_far = ((PAD + INNER * 0.30, BASE_Y),
               (PAD + INNER * 0.62, PAD + INNER * 0.50),
               (RIGHT_X, BASE_Y))

    for y in range(size):
        py = (y + 0.5) * inv
        # Degradado vertical del fondo: se resuelve una vez por fila.
        gy = _lerp(ACCENT_TOP[1], ACCENT_BOTTOM[1], py)
        grad_b = _lerp(ACCENT_TOP[2], ACCENT_BOTTOM[2], py)
        grad_r = _lerp(ACCENT_TOP[0], ACCENT_BOTTOM[0], py)
        row = y * size * 4
        for x in range(size):
            px = (x + 0.5) * inv
            r = 0.0
            g = 0.0
            b = 0.0
            a = 0.0

            # 1. Tarjeta con esquinas redondeadas y degradado del acento.
            cov = _coverage(_sd_round_rect(px, py, 0.5, 0.5, 0.5, 0.5,
                                           CARD_RADIUS), size)
            if cov > 0.0:
                a = cov
                r, g, b = grad_r, gy, grad_b

            # 2. Panel interior oscuro (visor).
            cov = _coverage(_sd_round_rect(px, py, *panel), size)
            if cov > 0.0:
                r = _lerp(r, CARD[0], cov)
                g = _lerp(g, CARD[1], cov)
                b = _lerp(b, CARD[2], cov)
                a = _lerp(a, 1.0, cov)

            # 3. Brillo superior sutil del panel.
            if py < highlight_bottom:
                cov = _coverage(_sd_round_rect(px, py, *panel), size) * (120.0 / 255.0)
                if cov > 0.0:
                    r = _lerp(r, CARD_LIGHT[0], cov)
                    g = _lerp(g, CARD_LIGHT[1], cov)
                    b = _lerp(b, CARD_LIGHT[2], cov)
                    a = _lerp(a, 1.0, cov)

            # 4. Sol.
            cov = _coverage(_sd_circle(px, py, SUN_CX, SUN_CY, SUN_R), size)
            if cov > 0.0:
                r = _lerp(r, SUN[0], cov)
                g = _lerp(g, SUN[1], cov)
                b = _lerp(b, SUN[2], cov)
                a = _lerp(a, 1.0, cov)

            # 5. Montanas (dos planos para dar profundidad).
            cov = _coverage(_sd_triangle(px, py, tri_near), size) * (235.0 / 255.0)
            if cov > 0.0:
                r = _lerp(r, MOUNTAIN[0], cov)
                g = _lerp(g, MOUNTAIN[1], cov)
                b = _lerp(b, MOUNTAIN[2], cov)
                a = _lerp(a, 1.0, cov)
            cov = _coverage(_sd_triangle(px, py, tri_far), size) * (235.0 / 255.0)
            if cov > 0.0:
                r = _lerp(r, MOUNTAIN_FAR[0], cov)
                g = _lerp(g, MOUNTAIN_FAR[1], cov)
                b = _lerp(b, MOUNTAIN_FAR[2], cov)
                a = _lerp(a, 1.0, cov)

            # 6. Linea de horizonte.
            cov = _coverage(_sd_round_rect(px, py, (HORIZON_X0 + HORIZON_X1) * 0.5,
                                           BASE_Y + HORIZON_H * 0.5,
                                           (HORIZON_X1 - HORIZON_X0) * 0.5,
                                           HORIZON_H * 0.5, HORIZON_H * 0.5), size)
            if cov > 0.0:
                r = _lerp(r, HORIZON[0], cov)
                g = _lerp(g, HORIZON[1], cov)
                b = _lerp(b, HORIZON[2], cov)
                a = _lerp(a, 1.0, cov)

            i = row + x * 4
            buf[i] = int(r + 0.5)
            buf[i + 1] = int(g + 0.5)
            buf[i + 2] = int(b + 0.5)
            buf[i + 3] = int(a * 255.0 + 0.5)
    return bytes(buf)


# ---------------------------------------------------------------------------
# Codificadores de imagen (biblioteca estandar)
# ---------------------------------------------------------------------------
def encode_png(size: int, rgba: bytes) -> bytes:
    """PNG RGBA de 8 bits (filtro 0 en todas las filas)."""
    raw = bytearray()
    stride = size * 4
    for y in range(size):
        raw.append(0)
        raw += rgba[y * stride:(y + 1) * stride]

    def chunk(tag: bytes, payload: bytes) -> bytes:
        body = tag + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + chunk(b"IEND", b""))


def encode_dib(size: int, rgba: bytes) -> bytes:
    """Entrada DIB del .ico: BITMAPINFOHEADER + BGRA (abajo->arriba) + mascara AND."""
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0,
                         size * size * 4, 0, 0, 0, 0)
    xor = bytearray()
    for y in range(size - 1, -1, -1):
        base = y * size * 4
        for x in range(size):
            i = base + x * 4
            xor += bytes((rgba[i + 2], rgba[i + 1], rgba[i], rgba[i + 3]))
    # Mascara AND: 1 bit por pixel, filas alineadas a 4 bytes. Se marcan los
    # pixeles totalmente transparentes para los consumidores que la ignoren.
    stride = ((size + 31) // 32) * 4
    mask = bytearray()
    for y in range(size - 1, -1, -1):
        bits = bytearray(stride)
        base = y * size * 4
        for x in range(size):
            if rgba[base + x * 4 + 3] == 0:
                bits[x >> 3] |= 0x80 >> (x & 7)
        mask += bits
    return header + bytes(xor) + bytes(mask)


def build_ico(images) -> bytes:
    """Empaqueta [(tamano, imagen)] en un .ico con directorio de 16 bytes/entrada."""
    entries = []
    for size, data in images:
        entries.append((size, data, PNG_FROM <= size))
    payload = bytearray(struct.pack("<HHH", 0, 1, len(entries)))
    offset = 6 + 16 * len(entries)
    for size, data, _ in entries:
        dim = 0 if size >= 256 else size      # 0 significa 256 en el formato ICO
        payload += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    for _, data, _ in entries:
        payload += data
    return bytes(payload)


# ---------------------------------------------------------------------------
# Punto de entrada
# ---------------------------------------------------------------------------
def generate(preview: bool) -> int:
    root = Path(__file__).resolve().parent.parent
    res_dir = root / "resources"
    res_dir.mkdir(parents=True, exist_ok=True)

    images = []
    for size in SIZES:
        rgba = render(size)
        data = encode_png(size, rgba) if size >= PNG_FROM else encode_dib(size, rgba)
        images.append((size, data))
        print(f"  {size:>3}x{size:<3} {len(data):>8} bytes"
              f"{'  (PNG)' if size >= PNG_FROM else '  (DIB 32bpp)'}")

    ico_path = res_dir / "artpicst.ico"
    ico_path.write_bytes(build_ico(images))
    print(f"Icono generado: {ico_path}")
    print(f"  {len(SIZES)} resoluciones, {ico_path.stat().st_size} bytes")

    if preview:
        png_path = res_dir / "artpicst.png"
        png_path.write_bytes(encode_png(SIZES[-1], render(SIZES[-1])))
        print(f"Vista previa PNG: {png_path}")

    # Autocomprobacion: el directorio debe poder recorrerse y cuadrar.
    blob = ico_path.read_bytes()
    count = struct.unpack_from("<H", blob, 4)[0]
    if count != len(SIZES):
        print(f"ERROR: el .ico contiene {count} imagenes, se esperaban {len(SIZES)}",
              file=sys.stderr)
        return 1
    for index in range(count):
        _, _, _, _, _, bpp, length, offset = struct.unpack_from("<BBBBHHII", blob, 6 + 16 * index)
        if bpp != 32 or offset + length > len(blob):
            print(f"ERROR: entrada {index} corrupta del .ico", file=sys.stderr)
            return 1
    print("Verificacion: OK (10 entradas de 32 bits, sin desbordamientos)")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Genera el icono multirresolucion de ARTPICST.")
    parser.add_argument("--preview", action="store_true",
                        help="escribe ademas resources/artpicst.png (vista previa 256x256)")
    args = parser.parse_args()
    return generate(args.preview)


if __name__ == "__main__":
    raise SystemExit(main())
