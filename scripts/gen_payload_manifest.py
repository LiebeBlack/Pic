# ============================================================================
# gen_payload_manifest.py — Genera installer/payload_manifest.hpp
# ----------------------------------------------------------------------------
# Calcula el SHA-256 (hex minúsculo) de cada archivo del payload autocontenido
# (resources/app/) y emite una cabecera C++ con la tabla que el instalador
# usa para verificar la integridad ANTES de extraer nada a disco.
#
# El instalador falla con exit code 2 si un hash no coincide: un payload
# corrupto o manipulado nunca se instala.
#
# Uso:  python scripts/gen_payload_manifest.py --dir resources/app \
#           --out installer/payload_manifest.hpp
#       (los valores por defecto son los del pipeline del proyecto)
# ============================================================================
import argparse
import hashlib
import sys
from pathlib import Path

# (nombre en disco, nombre simbólico, id de recurso RCDATA en el .rc)
PAYLOAD = [
    ("artpicst.exe",         "APP_EXE",     201),
    ("artpicst.ico",         "APP_ICO",     202),
    ("version.json",         "APP_VERSION", 203),
    ("README.md",            "APP_README",  204),
    ("artpicst_updater.exe", "APP_UPDATER", 205),
]

HEADER_TEMPLATE = """\
// ============================================================================
// payload_manifest.hpp — GENERADO por scripts/gen_payload_manifest.py
// ----------------------------------------------------------------------------
// SHA-256 (hex minúsculo) de cada archivo del payload incrustado como RCDATA.
// El instalador verifica cada recurso contra esta tabla ANTES de escribir en
// disco; cualquier discrepancia aborta la instalación (exit code 2).
//
// NO editar a mano: se regenera en cada build (collect_app_payload.py lo
// invoca automáticamente). Las cadenas vacías significan "sin verificar"
// (compilaciones de desarrollo sin payload poblado).
// ============================================================================
#pragma once

#include <cstddef>

namespace artpicst {
namespace manifest {

struct PayloadEntry {{
    unsigned    resourceId;      // id RCDATA en artpicst_installer.rc
    const char* fileName;        // nombre en el directorio de instalación
    const char* sha256Hex;       // 64 chars hex, o "" si no hay referencia
}};

inline constexpr PayloadEntry kPayload[] = {{
{entries}
}};

inline constexpr std::size_t kPayloadCount = sizeof(kPayload) / sizeof(kPayload[0]);

}}  // namespace manifest
}}  // namespace artpicst
"""


def sha256_hex(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    here = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description="Genera el manifest SHA-256 del payload")
    parser.add_argument("--dir", type=Path, default=here.parent / "resources" / "app",
                        help="Directorio con los archivos del payload")
    parser.add_argument("--out", type=Path, default=here.parent / "installer" / "payload_manifest.hpp",
                        help="Cabecera C++ de salida")
    args = parser.parse_args()

    payload_dir: Path = args.dir.resolve()
    rows = []
    missing = []
    for file_name, symbol, res_id in PAYLOAD:
        src = payload_dir / file_name
        if src.is_file():
            digest = sha256_hex(src)
            size = src.stat().st_size
            print(f"  manifest: {file_name:22s} sha256={digest[:16]}… ({size:,} bytes)")
        else:
            digest = ""
            missing.append(file_name)
        rows.append(f'    {{ {res_id:3d}, "{file_name}", "{digest}" }},   // RES_{symbol}')

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        HEADER_TEMPLATE.format(entries="\n".join(rows)), encoding="utf-8", newline="\n")
    print(f"  manifest: {args.out} escrito ({len(rows)} entradas)")

    if missing:
        # No es un error fatal: las compilaciones de desarrollo no tienen
        # payload poblado. El instalador marcará esas entradas como no
        // verificables y lo registrará en su log.
        print(f"  AVISO: sin payload para: {', '.join(missing)} (hashes vacíos)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
