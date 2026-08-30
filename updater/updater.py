import json
import os
import re
import subprocess
import sys
import tempfile
import threading
import time
import webbrowser
from pathlib import Path

import requests
import win32api
import win32con
import win32gui

from config import (
    APP_NAME,
    AUTO_OPEN_AFTER_INSTALL,
    CURRENT_VERSION,
    GITHUB_OWNER,
    GITHUB_REPO,
    INSTALLER_ARGS,
    INSTALLER_ASSET_NAME,
    INSTALLER_ASSET_NAME,
    INSTALL_REG_KEY,
    RELEASES_API,
)


def log(msg):
    print(f"[{time.strftime('%H:%M:%S')}] {msg}")


def get_latest_release():
    url = RELEASES_API.format(owner=GITHUB_OWNER, repo=GITHUB_REPO)
    r = requests.get(url, timeout=20)
    r.raise_for_status()
    data = r.json()
    tag = data.get("tag_name", "").strip()
    assets = data.get("assets", [])
    installer_url = None
    for asset in assets:
        name = asset.get("name", "")
        if name.lower() == INSTALLER_ASSET_NAME.lower():
            installer_url = asset.get("browser_download_url")
            break
    return tag, installer_url


def normalize_version(v):
    v = str(v or "0").strip().lower()
    v = re.sub(r"[^0-9.]", "", v)
    parts = [int(p) for p in v.split(".") if p]
    while len(parts) < 4:
        parts.append(0)
    return tuple(parts[:4])


def is_update_available(current_version, latest_tag):
    return normalize_version(latest_tag) > normalize_version(current_version)


def is_installed():
    try:
        with win32api.RegOpenKeyEx(win32con.HKEY_LOCAL_MACHINE, INSTALL_REG_KEY, 0, win32con.KEY_READ) as key:
            return True
    except Exception:
        return False


def uninstall_existing():
    try:
        with win32api.RegOpenKeyEx(win32con.HKEY_LOCAL_MACHINE, INSTALL_REG_KEY, 0, win32con.KEY_READ) as key:
            value, _ = win32api.RegQueryValueEx(key, "UninstallString")
            if value:
                subprocess.run([value, "/S"], check=False)
                return True
    except Exception:
        pass
    return False


def download_file(url, dest_path):
    response = requests.get(url, timeout=60, stream=True)
    response.raise_for_status()
    with open(dest_path, "wb") as f:
        for chunk in response.iter_content(chunk_size=8192):
            if chunk:
                f.write(chunk)


def install_updater(path):
    log(f"Instalando actualizacion: {path}")
    try:
        if AUTO_OPEN_AFTER_INSTALL:
            subprocess.Popen([path, INSTALLER_ARGS], shell=True)
        else:
            subprocess.run([path, INSTALLER_ARGS], check=False)
        return True
    except Exception as exc:
        log(f"Error al instalar: {exc}")
        return False


def prompt_user_for_update(latest_tag, installer_url):
    import ctypes
    MB_YESNO = 0x00000004
    MB_ICONQUESTION = 0x00000020
    result = ctypes.windll.user32.MessageBoxW(
        0,
        f"Hay una nueva version de {APP_NAME}: {latest_tag}\n\nDesea descargarla e instalarla ahora?",
        f"Actualizacion disponible - {APP_NAME}",
        MB_YESNO | MB_ICONQUESTION,
    )
    return result == 6


def run():
    log(f"Comprobando actualizaciones de {APP_NAME}...")
    try:
        latest_tag, installer_url = get_latest_release()
    except Exception as exc:
        log(f"No se pudo consultar GitHub: {exc}")
        return 1

    if not latest_tag:
        log("No se encontro release valida.")
        return 0

    log(f"Version actual: {CURRENT_VERSION} | ultima: {latest_tag}")
    if not is_update_available(CURRENT_VERSION, latest_tag):
        log("Tu programa ya está actualizado.")
        return 0

    if not installer_url:
        log("No se encontro el instalador de la ultima version.")
        return 0

    if not prompt_user_for_update(latest_tag, installer_url):
        log("Actualizacion cancelada por el usuario.")
        return 0

    temp_dir = tempfile.gettempdir()
    installer_path = os.path.join(temp_dir, INSTALLER_ASSET_NAME)
    try:
        download_file(installer_url, installer_path)
    except Exception as exc:
        log(f"Error al descargar: {exc}")
        return 1

    if is_installed():
        log("Desinstalando version actual antes de instalar la nueva...")
        uninstall_existing()
        time.sleep(2)

    if install_updater(installer_path):
        log("Instalador lanzado correctamente.")
    else:
        log("No se pudo lanzar el instalador.")
    return 0


if __name__ == "__main__":
    sys.exit(run())
