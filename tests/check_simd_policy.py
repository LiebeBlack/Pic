#!/usr/bin/env python3
# ============================================================================
# check_simd_policy.py — Gate de política SIMD + oráculo de identidades enteras
# ----------------------------------------------------------------------------
# ARTPICST exige un baseline SSE4.2 y PROHÍBE AVX/AVX2/AVX-512. Este script es
# el guardián automático de esa política y, además, un oráculo independiente
# (no depende del compilador) de las identidades enteras y de las máscaras
# PSHUFB que usan los kernels de src/image_core.hpp.
#
# Comprueba:
#   A. Que ninguna fuente, script ni workflow active AVX (tokens prohibidos).
#   B. Que los flags SSE4.2 estén presentes en TODOS los puntos de build.
#   C. Que las identidades enteras del núcleo sean exactas (LUT, /255, BT.601).
#   D. Que las máscaras PSHUFB escritas a mano hagan EXACTAMENTE lo que dice su
#      comentario (emulación de _mm_shuffle_epi8 leída del propio fuente).
#
# Uso:  python tests/check_simd_policy.py [--root .]
# Exit: 0 = política cumplida · 1 = violación (se imprime el detalle)
#
# Sin dependencias externas: solo la biblioteca estándar.
# ============================================================================
from __future__ import annotations

import argparse
import re
import sys
from fractions import Fraction
from pathlib import Path

# ---------------------------------------------------------------------------
# A. Tokens prohibidos: cualquier cosa que habilite AVX/AVX2/AVX-512
# ---------------------------------------------------------------------------
# Se excluye include/ porque los headers de terceros (stb_image*.h) contienen
# sus propias ramas AVX que nunca se activan: el build define STBIR_NO_AVX y
# STBIR_NO_AVX2 y compila con /arch:SSE4.2 / -msse4.2.
SCAN_DIRS = ("src", "installer", "updater", "tests", "scripts", ".github")
SCAN_EXTRA_FILES = ("CMakeLists.txt", "build.ps1", "build.bat", "build_mingw.bat", "Makefile")
SKIP_DIR_NAMES = {".git", "build", "dist", "__pycache__", "app", "node_modules"}
# Este propio script queda fuera del barrido: sus patrones y sus descripciones
# CONTIENEN literalmente los tokens prohibidos (es la definición de la política,
# no una violación de ella).
SELF_REL = "tests/check_simd_policy.py"

FORBIDDEN = [
    (re.compile(r"\b__m256[di]?\b"), "__m256* (registro AVX)"),
    (re.compile(r"\b__m512[di]?\b"), "__m512* (registro AVX-512)"),
    (re.compile(r"\b_mm256_\w+"), "_mm256_* (intrínseco AVX)"),
    (re.compile(r"\b_mm512_\w+"), "_mm512_* (intrínseco AVX-512)"),
    (re.compile(r"/arch:\s*AVX", re.IGNORECASE), "/arch:AVX* (habilita AVX)"),
    (re.compile(r"-mavx"), "-mavx* (habilita AVX)"),
    (re.compile(r"-march\s*=\s*native"), "-march=native (habilita lo que soporte la CPU)"),
    (re.compile(r"-march\s*=\s*(haswell|skylake|znver\d|broadwell|ivybridge|sandybridge)"),
     "-march=<microarquitectura con AVX>"),
]

# Warnings (no fallan el gate, pero se avisa).
WARN_ONLY = [
    (re.compile(r"#\s*include\s*[<\"]immintrin\.h[>\"]"),
     "immintrin.h agrega TODOS los headers SIMD; preferir nmmintrin.h/smmintrin.h"),
]

# ---------------------------------------------------------------------------
# B. Flags SSE4.2 obligatorios por punto de build
# ---------------------------------------------------------------------------
REQUIRED_FLAGS = [
    ("build.ps1", "/arch:SSE4.2"),
    ("build.bat", "/arch:SSE4.2"),
    ("CMakeLists.txt", "/arch:SSE4.2"),
    ("build_mingw.bat", "-msse4.2"),
    ("Makefile", "-msse4.2"),
]
# Los workflows se revisan aparte (hay varios y todos deben llevarlo).
WORKFLOW_FLAG = "/arch:SSE4.2"


def iter_scan_files(root: Path):
    seen: set[Path] = set()
    for d in SCAN_DIRS:
        base = root / d
        if not base.is_dir():
            continue
        for p in sorted(base.rglob("*")):
            if not p.is_file() or p in seen:
                continue
            if any(part in SKIP_DIR_NAMES for part in p.parts):
                continue
            seen.add(p)
            yield p
    for name in SCAN_EXTRA_FILES:
        p = root / name
        if p.is_file() and p not in seen:
            seen.add(p)
            yield p


def check_forbidden_tokens(root: Path) -> list[str]:
    problems: list[str] = []
    warnings: list[str] = []
    for p in iter_scan_files(root):
        try:
            text = p.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        rel = p.relative_to(root).as_posix()
        if rel == SELF_REL:
            continue
        for lineno, line in enumerate(text.splitlines(), 1):
            for rx, what in FORBIDDEN:
                if rx.search(line):
                    problems.append(f"{rel}:{lineno}: {what} -> {line.strip()[:110]}")
            for rx, what in WARN_ONLY:
                if rx.search(line):
                    warnings.append(f"{rel}:{lineno}: {what}")
    for w in warnings:
        print(f"[aviso] {w}")
    return problems


def check_required_flags(root: Path) -> list[str]:
    problems: list[str] = []
    for name, flag in REQUIRED_FLAGS:
        p = root / name
        if not p.is_file():
            problems.append(f"falta {name}: no se puede verificar el flag {flag}")
            continue
        if flag not in p.read_text(encoding="utf-8", errors="ignore"):
            problems.append(f"{name}: no contiene '{flag}'")
    wf_dir = root / ".github" / "workflows"
    if wf_dir.is_dir():
        for wf in sorted(wf_dir.glob("*.yml")):
            text = wf.read_text(encoding="utf-8", errors="ignore")
            # Solo los workflows que invocan cl.exe necesitan el flag.
            if "cl " in text or "cl.exe" in text:
                if WORKFLOW_FLAG not in text:
                    problems.append(
                        f".github/workflows/{wf.name}: compila con cl pero no usa '{WORKFLOW_FLAG}'")
    return problems


# ---------------------------------------------------------------------------
# C. Oráculo de las identidades enteras del núcleo (aritmética EXACTA)
# ---------------------------------------------------------------------------
def cpp_floor_div(num: int, den: int) -> int:
    """Replica exacta de artpicst::FloorDiv (división entera hacia abajo)."""
    if den == 0:
        raise ZeroDivisionError
    if num >= 0:
        return num // den
    return -((-num + den - 1) // den)


def check_integer_identities() -> list[str]:
    problems: list[str] = []

    # C.1 LUT de claridad: clamp(FloorDiv(56*v - 743, 50)) debe ser exactamente
    # clamp(floor(128 + (v-128)*1.12 + 0.5)). Se usa Fraction para que la
    # comparación sea matemáticamente exacta (1.12 = 28/25).
    mismatch_lut = []
    for v in range(256):
        cpp = max(0, min(255, cpp_floor_div(56 * v - 743, 50)))
        exact = Fraction(128) + (Fraction(v) - 128) * Fraction(112, 100) + Fraction(1, 2)
        ref = exact.numerator // exact.denominator          # floor
        ref = max(0, min(255, ref))
        if cpp != ref:
            mismatch_lut.append((v, cpp, ref))
    if mismatch_lut:
        problems.append(f"LUT de claridad: {len(mismatch_lut)} discrepancias, "
                        f"p.ej. {mismatch_lut[:4]}")

    # C.2 Premultiplicado sin división: (c*a + 127)/255 == ((c*a + 127) * 0x8081) >> 23
    # (es lo que hace mulhi_epu16(x, 0x8081) >> 7 en el kernel SIMD).
    bad_premul = []
    for a in range(256):
        for c in range(256):
            x = c * a + 127
            if x // 255 != (x * 0x8081) >> 23:
                bad_premul.append((c, a))
                if len(bad_premul) > 8:
                    break
        if len(bad_premul) > 8:
            break
    if bad_premul:
        problems.append(f"identidad de premultiplicado /255: discrepancias en {bad_premul[:8]}")

    # C.3 BT.601 en punto fijo: (299r + 587g + 114b)/1000 nunca debe exceder 255
    worst = max((299 * r + 587 * g + 114 * b) // 1000
                for r in (0, 255) for g in (0, 255) for b in (0, 255))
    if worst > 255:
        problems.append(f"BT.601 en punto fijo desborda uint8_t (máximo {worst})")

    # C.4 El divisor debe ser exactamente la suma de los pesos (si no, la media
    # ponderada pierde el rango completo).
    if 299 + 587 + 114 != 1000:
        problems.append("pesos BT.601 y divisor incoherentes")
    return problems


# ---------------------------------------------------------------------------
# D. Emulación de _mm_shuffle_epi8 sobre las máscaras escritas en la cabecera
# ---------------------------------------------------------------------------
def set_epi32_to_bytes(args_hex: list[int]) -> list[int]:
    """_mm_set_epi32(e3, e2, e1, e0): los dwords van de mayor a menor."""
    e3, e2, e1, e0 = args_hex
    out: list[int] = []
    for dword in (e0, e1, e2, e3):
        out.extend([(dword >> (8 * i)) & 0xFF for i in range(4)])
    return out


def emulate_pshufb(src_bytes: list[int], mask: list[int]) -> list[int]:
    """Semántica de PSHUFB: out[i] = src[mask[i] & 0x0F] salvo si mask[i] & 0x80,
    en cuyo caso out[i] = 0."""
    out = []
    for m in mask:
        out.append(0 if (m & 0x80) else src_bytes[m & 0x0F])
    return out


def extract_call_int_args(text: str, pattern: str) -> list[int] | None:
    """Extrae los 4 argumentos de una llamada a _mm_set_epi32 localizada por
    `pattern` (una expresión regular con UN grupo de captura sobre los args)."""
    rx = re.compile(pattern, re.DOTALL)
    m = rx.search(text)
    if not m:
        return None
    out = []
    for tok in m.group(1).split(","):
        tok = tok.strip()
        if not tok:
            continue
        # Acepta 0x... y static_cast<int>(0x...u)
        hm = re.search(r"0x([0-9A-Fa-f]+)", tok)
        if not hm:
            return None
        out.append(int(hm.group(1), 16))
    return out if len(out) == 4 else None


def check_pshufb_masks(root: Path) -> list[str]:
    problems: list[str] = []
    header = root / "src" / "image_core.hpp"
    if not header.is_file():
        return [f"no existe {header}"]
    text = header.read_text(encoding="utf-8", errors="ignore")

    # D.1 swapMask: intercambio B<->R dejando G y A intactos.
    # Pixel BGRA de entrada = [B, G, R, A] (bytes 0..3 en little endian).
    swap_args = extract_call_int_args(
        text, r"swapMask\s*=\s*_mm_set_epi32\(([^)]*)\)")
    if swap_args is None:
        problems.append("no se pudo localizar la definición de swapMask en image_core.hpp")
    else:
        mask = set_epi32_to_bytes(swap_args)
        expected = [2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14, 13, 12, 15]
        if mask != expected:
            problems.append(f"swapMask no intercambia B<->R: {mask} != {expected}")
        else:
            pixel = [0xB0, 0x80, 0x70, 0xA0, 0x0B, 0x08, 0x07, 0x0A,
                     0xF0, 0xE0, 0xD0, 0xC0, 0x3B, 0x38, 0x37, 0x3A]
            got = emulate_pshufb(pixel, mask)
            want = [0x70, 0x80, 0xB0, 0xA0, 0x07, 0x08, 0x0B, 0x0A,
                    0xD0, 0xE0, 0xF0, 0xC0, 0x37, 0x38, 0x3B, 0x3A]
            if got != want:
                problems.append(f"swapMask (PSHUFB) produce {got} en vez de {want}")

    # D.2 aRep: réplica del alfa en los carriles de 16 bits con G y A a cero.
    # Debe dejar cada dword como [A, 0, A, 0] (bytes B=A, G=0, R=A, A=0), que es
    # lo que espera el kernel (0x00AA en cada carril de 16 bits).
    # En la cabecera la máscara va DENTRO de la llamada a _mm_shuffle_epi8:
    #   const __m128i aRep = _mm_shuffle_epi8(v, _mm_set_epi32(...));
    rep_args = extract_call_int_args(
        text, r"aRep\s*=\s*_mm_shuffle_epi8\(\s*v\s*,\s*_mm_set_epi32\(([^)]*)\)")
    if rep_args is None:
        problems.append("no se pudo localizar la definición de aRep en image_core.hpp")
    else:
        mask = set_epi32_to_bytes(rep_args)
        expected_dword = [3, 0x80, 3, 0x80, 7, 0x80, 7, 0x80,
                          11, 0x80, 11, 0x80, 15, 0x80, 15, 0x80]
        if mask != expected_dword:
            problems.append(f"máscara de réplica de alfa incorrecta: {mask} != {expected_dword}")
        else:
            pixel = [0xB0, 0x80, 0x70, 0xAA, 0x0B, 0x08, 0x07, 0x11,
                     0xF0, 0xE0, 0xD0, 0x00, 0x3B, 0x38, 0x37, 0xFF]
            got = emulate_pshufb(pixel, mask)
            want = [0xAA, 0x00, 0xAA, 0x00, 0x11, 0x00, 0x11, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00]
            if got != want:
                problems.append(f"réplica de alfa (PSHUFB) produce {got} en vez de {want}")

    # D.3 El núcleo SSE2 y el SSE4.1 deben aplicar la MISMA identidad de
    # redondeo: se exige que el literal mágico siga presente.
    if "0x8081" not in text:
        problems.append("falta la constante mágica 0x8081 del premultiplicado")
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description="Gate de política SIMD de ARTPICST")
    parser.add_argument("--root", type=Path,
                        default=Path(__file__).resolve().parent.parent,
                        help="Raíz del repositorio")
    args = parser.parse_args()
    root: Path = args.root.resolve()

    problems: list[str] = []

    print("== Política SIMD de ARTPICST ==")
    print(f"Raíz: {root}")

    problems += check_forbidden_tokens(root)
    problems += check_required_flags(root)
    problems += check_integer_identities()
    problems += check_pshufb_masks(root)

    if problems:
        print()
        print(f"[FALLO] {len(problems)} violación(es) de política:")
        for p in problems:
            print(f"  - {p}")
        return 1

    print("[OK] Sin AVX en fuentes/scripts/workflows.")
    print("[OK] Flags SSE4.2 presentes en todos los puntos de build.")
    print("[OK] Identidades enteras exactas (LUT de claridad, /255, BT.601).")
    print("[OK] Máscaras PSHUFB verificadas por emulación.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
