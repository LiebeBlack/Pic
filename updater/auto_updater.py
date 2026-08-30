import ctypes
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import requests
import win32api
import win32con

from config import (
    APP_NAME,
    AUTO_OPEN_AFTER_INSTALL,
    CURRENT_VERSION,
    GITHUB_OWNER,
    GITHUB_REPO,
    INSTALLER_ARGS,
    INSTALLER_ASSET_NAME,
    INSTALL_REG_KEY,
    RELEASES_API,
)


def log(message):
    print(f"[{time.strftime('%H:%M:%S')}] {message}")


def normalize_version(version):
    value = str(version or "0").strip().lower()

    # Soporta formatos como: auto-23, auto-9, v1.2.3, 1.2.3, 1.0.0.0
    if value.startswith("auto-"):
        value = value.replace("auto-", "")
    elif value.startswith("v"):
        value = value[1:]

    value = re.sub(r"[^0-9.]", "", value)
    if not value:
        return (0, 0, 0, 0)

    parts = [int(p) for p in value.split(".") if p]
    while len(parts) < 4:
        parts.append(0)
    return tuple(parts[:4])


def get_latest_release():
    url = RELEASES_API.format(owner=GITHUB_OWNER, repo=GITHUB_REPO)
    response = requests.get(url, timeout=20)
    response.raise_for_status()
    payload = response.json()
    tag_name = str(payload.get("tag_name", "")).strip()
    installer_url = None
    for asset in payload.get("assets", []):
        name = str(asset.get("name", "")).lower()
        if name == INSTALLER_ASSET_NAME.lower():
            installer_url = asset.get("browser_download_url")
            break
    return tag_name, installer_url


def is_update_available(current_version, latest_tag):
    return normalize_version(latest_tag) > normalize_version(current_version)


def get_installed_uninstall_string():
    try:
        key = win32api.RegOpenKeyEx(win32con.HKEY_LOCAL_MACHINE, INSTALL_REG_KEY, 0, win32con.KEY_READ)
        try:
            value, _ = win32api.RegQueryValueEx(key, "UninstallString")
            return value
        finally:
            key.Close()
    except Exception:
        return None


def uninstall_existing():
    uninstall_string = get_installed_uninstall_string()
    if not uninstall_string:
        return False
    try:
        command = uninstall_string.strip()
        if command.lower().endswith("uninstall.exe"):
            subprocess.run([command, "/S"], check=False)
        else:
            subprocess.run(command, check=False)
        return True
    except Exception as exc:
        log(f"Error al desinstalar: {exc}")
        return False


def download_file(url, destination):
    response = requests.get(url, timeout=60, stream=True)
    response.raise_for_status()
    with open(destination, "wb") as handle:
        for chunk in response.iter_content(chunk_size=8192):
            if chunk:
                handle.write(chunk)


def run_installer(installer_path):
    try:
        command = [installer_path]
        if INSTALLER_ARGS:
            command.extend(shlex.split(INSTALLER_ARGS))

        if AUTO_OPEN_AFTER_INSTALL:
            subprocess.Popen(command, shell=False)
        else:
            subprocess.run(command, check=False)
        return True
    except Exception as exc:
        log(f"No se pudo lanzar el instalador: {exc}")
        return False


def show_update_dialog(latest_tag):
    result = ctypes.windll.user32.MessageBoxW(
        0,
        f"Hay una nueva version de {APP_NAME}: {latest_tag}\n\nDesea descargarla e instalarla ahora?",
        f"Actualizacion disponible - {APP_NAME}",
        0x00000004 | 0x00000020,
    )
    return result == 6


def check_for_updates(show_dialog=False):
    try:
        latest_tag, installer_url = get_latest_release()
    except Exception as exc:
        log(f"No se pudo contactar con GitHub: {exc}")
        return False

    if not latest_tag:
        log("No existe release valida en GitHub.")
        return False

    log(f"Version actual: {CURRENT_VERSION} | ultima disponible: {latest_tag}")
    if not is_update_available(CURRENT_VERSION, latest_tag):
        log("La aplicacion ya esta actualizada.")
        return False

    if not installer_url:
        log("No se encontro el instalador en la ultima release.")
        return False

    if show_dialog and not show_update_dialog(latest_tag):
        log("Actualizacion cancelada por el usuario.")
        return False

    temp_dir = tempfile.gettempdir()
    installer_path = os.path.join(temp_dir, INSTALLER_ASSET_NAME)
    try:
        download_file(installer_url, installer_path)
        log(f"Instalador descargado: {installer_path}")
    except Exception as exc:
        log(f"Error al descargar el instalador: {exc}")
        return False

    if get_installed_uninstall_string():
        log("Se desinstalara la version previa antes de instalar la nueva.")
        uninstall_existing()
        time.sleep(2)

    if run_installer(installer_path):
        log("Instalacion lanzada correctamente.")
        return True
    return False


def main():
    log(f"Comprobando actualizaciones de {APP_NAME}...")
    check_for_updates(show_dialog=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
