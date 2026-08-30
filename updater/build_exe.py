import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "updater_build"
DIST = ROOT / "dist"


def ensure_pyinstaller():
    subprocess.run([sys.executable, "-m", "pip", "install", "pyinstaller"], check=False)


def build():
    OUT.mkdir(exist_ok=True)
    DIST.mkdir(exist_ok=True)

    script = ROOT / "updater" / "auto_updater.py"
    cmd = [
        sys.executable,
        "-m",
        "PyInstaller",
        "--onefile",
        "--noconsole",
        "--icon",
        str(ROOT / "resources" / "artpicst.ico"),
        "--distpath",
        str(OUT),
        "--workpath",
        str(OUT / "build"),
        "--specpath",
        str(OUT / "spec"),
        str(script),
    ]
    subprocess.run(cmd, check=True)

    exe = OUT / "auto_updater.exe"
    target = DIST / "auto_updater.exe"
    if exe.exists():
        shutil.copy2(exe, target)
        print(f"EXE creado: {exe}")
        print(f"Copia lista: {target}")
    else:
        print("No se encontro el ejecutable generado.")


if __name__ == "__main__":
    ensure_pyinstaller()
    build()
