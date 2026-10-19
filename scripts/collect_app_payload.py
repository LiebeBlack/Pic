#!/usr/bin/env python3
# ============================================================================
# collect_app_payload.py — Prepara el payload autocontenido del instalador
# ----------------------------------------------------------------------------
# Copia los binarios recién compilados (visor, updater, icono, version.json y
# README) a resources/app/, desde donde artpicst_installer.rc los incrusta
# como recursos RCDATA. Así el instalador es una unidad única distribuible.
#
# Uso:  python scripts/collect_app_payload.py --root <raíz del repo>
#       (por defecto usa la raíz inferida desde la ubicación del script)
# ============================================================================
import argparse
import shutil
import subprocess
import sys
from pathlib import Path

REQUIRED = [
    "artpicst.exe",
    "artpicst.ico",
    "version.json",
    "README.md",
    "artpicst_updater.exe",
]


def main() -> int:
    here = Path(__file__).resolve().parent
    default_root = here.parent
    parser = argparse.ArgumentParser(description="Puebla resources/app/ para el instalador autocontenido")
    parser.add_argument("--root", type=Path, default=default_root, help="Raíz del repositorio")
    args = parser.parse_args()

    root: Path = args.root.resolve()
    build = root / "build"
    payload = root / "resources" / "app"
    payload.mkdir(parents=True, exist_ok=True)

    sources = {
        "artpicst.exe": build / "artpicst.exe",
        "artpicst_updater.exe": build / "artpicst_updater.exe",
        "artpicst.ico": root / "resources" / "artpicst.ico",
        "version.json": root / "version.json",
        "README.md": root / "README.md",
    }

    missing = [name for name, src in sources.items() if not src.is_file()]
    if missing:
        print("ERROR: faltan archivos de entrada para el payload:")
        for name in missing:
            print(f"  - {sources[name]}")
        print("Compila primero artpicst.exe y artpicst_updater.exe (build.ps1 / CI).")
        return 1

    for name, src in sources.items():
        dst = payload / name
        shutil.copyfile(src, dst)
        print(f"  payload: {name} <- {src} ({dst.stat().st_size:,} bytes)")

    # Verificación final: los 5 archivos que artpicst_installer.rc exige.
    still_missing = [name for name in REQUIRED if not (payload / name).is_file()]
    if still_missing:
        print("ERROR: el payload quedó incompleto: " + ", ".join(still_missing))
        return 1

    # Manifest SHA-256 del payload: el instalador exige estos hashes antes de
    # extraer nada a disco (scripts/gen_payload_manifest.py).
    gen = here / "gen_payload_manifest.py"
    if gen.is_file():
        result = subprocess.run(
            [sys.executable, str(gen), "--dir", str(payload),
             "--out", str(root / "installer" / "payload_manifest.hpp")],
            check=False)
        if result.returncode != 0:
            print("ERROR: no se pudo generar installer/payload_manifest.hpp")
            return 1
    else:
        print("AVISO: falta scripts/gen_payload_manifest.py; el payload no tendrá hashes")

    print(f"Payload autocontenido listo en {payload}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
