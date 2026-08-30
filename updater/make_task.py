"""
Utilidad Python para registrar o eliminar la tarea programada de ARTPICST AutoUpdater.
"""

import subprocess
import sys
from pathlib import Path

TASK_NAME = "ARTPICST AutoUpdater"


def get_updater_exe() -> Path:
    root = Path(__file__).resolve().parent.parent
    candidates = [
        root / "dist" / "auto_updater.exe",
        root / "updater_build" / "auto_updater.exe",
        root / "auto_updater.exe",
    ]
    for c in candidates:
        if c.exists():
            return c
    return root / "dist" / "auto_updater.exe"


def create_task():
    exe_path = get_updater_exe()
    if not exe_path.exists():
        print(f"[ARTPICST] No se encontró el ejecutable en: {exe_path}")
        return False

    cmd = [
        "schtasks",
        "/Create",
        "/TN",
        TASK_NAME,
        "/TR",
        f'"{exe_path}" --silent',
        "/SC",
        "DAILY",
        "/MO",
        "2",
        "/ST",
        "09:00",
        "/F",
    ]
    try:
        subprocess.run(cmd, check=True)
        print(f"[ARTPICST] Tarea '{TASK_NAME}' creada con éxito.")
        return True
    except subprocess.CalledProcessError as err:
        print(f"[ARTPICST] Error al crear la tarea: {err}")
        return False


def delete_task():
    try:
        subprocess.run(["schtasks", "/Delete", "/TN", TASK_NAME, "/F"], check=True)
        print(f"[ARTPICST] Tarea '{TASK_NAME}' eliminada.")
        return True
    except Exception:
        return False


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] in ("--delete", "-d"):
        delete_task()
    else:
        create_task()
