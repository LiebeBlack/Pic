"""
Compilador de auto_updater.exe usando PyInstaller.
Empaqueta auto_updater.py en un único ejecutable sin consola para Windows.
"""

import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "updater_build"
DIST = ROOT / "dist"


def ensure_pyinstaller():
    try:
        import PyInstaller
    except ImportError:
        print("Instalando PyInstaller...")
        subprocess.run([sys.executable, "-m", "pip", "install", "pyinstaller"], check=True)


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    DIST.mkdir(parents=True, exist_ok=True)

    script = ROOT / "updater" / "auto_updater.py"
    icon_path = ROOT / "resources" / "artpicst.ico"

    cmd = [
        sys.executable,
        "-m",
        "PyInstaller",
        "--name=auto_updater",
        "--onefile",
        "--noconsole",
        "--clean",
        f"--paths={ROOT / 'updater'}",
        "--distpath",
        str(OUT),
        "--workpath",
        str(OUT / "build"),
        "--specpath",
        str(OUT / "spec"),
    ]

    if icon_path.exists():
        cmd.extend(["--icon", str(icon_path)])

    cmd.append(str(script))

    print(f"Ejecutando PyInstaller: {' '.join(cmd)}")
    subprocess.run(cmd, check=True)

    exe = OUT / "auto_updater.exe"
    target = DIST / "auto_updater.exe"
    if exe.exists():
        shutil.copy2(exe, target)
        print(f"-> Ejecutable generado: {exe}")
        print(f"-> Copiado a dist: {target}")
    else:
        print("ERROR: No se encontró el ejecutable generado.")
        sys.exit(1)


if __name__ == "__main__":
    ensure_pyinstaller()
    build()
