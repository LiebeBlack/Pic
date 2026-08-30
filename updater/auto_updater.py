"""
ARTPICST Auto Updater
Comprueba actualizaciones en GitHub Releases, descarga el instalador e instala de forma desatendida o interactiva.
No requiere dependencias externas de terceros (funciona con la biblioteca estándar de Python).
"""

import argparse
import ctypes
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
import winreg
from pathlib import Path

try:
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
except ImportError:
    APP_NAME = "ARTPICST"
    AUTO_OPEN_AFTER_INSTALL = True
    CURRENT_VERSION = "auto-28"
    GITHUB_OWNER = "LiebeBlack"
    GITHUB_REPO = "Pic"
    INSTALLER_ARGS = "/S"
    INSTALLER_ASSET_NAME = "artpicst-installer.exe"
    INSTALL_REG_KEY = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\ARTPICST"
    RELEASES_API = "https://api.github.com/repos/{owner}/{repo}/releases/latest"


def log(msg: str) -> None:
    timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
    print(f"[{timestamp}] [ARTPICST Updater] {msg}")


def normalize_version(ver_str: str) -> tuple:
    """
    Convierte tags como 'auto-28', 'v1.2.3', '1.0.4', '22' en una tupla comparable de enteros.
    """
    raw = str(ver_str or "0").strip().lower()
    raw = re.sub(r"^(auto|release|v)[-_]?", "", raw)
    nums = re.findall(r"\d+", raw)
    if not nums:
        return (0, 0, 0, 0)
    parts = [int(n) for n in nums]
    while len(parts) < 4:
        parts.append(0)
    return tuple(parts[:4])


def get_current_installed_version() -> str:
    """
    Detecta de forma inteligente la versión actualmente instalada en el equipo:
    1. Lee version.json en la carpeta del ejecutable o en Program Files
    2. Lee DisplayVersion del Registro de Windows (HKCU y HKLM)
    3. Retorna CURRENT_VERSION de config.py como respaldo
    """
    app_dir = Path(__file__).resolve().parent.parent
    candidate_json_paths = [
        app_dir / "version.json",
        app_dir / "dist" / "version.json",
        Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / APP_NAME / "version.json",
    ]
    for p in candidate_json_paths:
        if p.exists():
            try:
                with open(p, "r", encoding="utf-8") as f:
                    data = json.load(f)
                    val = data.get("version") or data.get("tag") or data.get("tag_name")
                    if val and str(val).strip():
                        return str(val).strip()
            except Exception:
                pass

    roots = [
        (winreg.HKEY_LOCAL_MACHINE, "HKLM"),
        (winreg.HKEY_CURRENT_USER, "HKCU"),
    ]
    flags = [winreg.KEY_READ | winreg.KEY_WOW64_64KEY, winreg.KEY_READ]

    for hkey_root, _ in roots:
        for access in flags:
            try:
                with winreg.OpenKey(hkey_root, INSTALL_REG_KEY, 0, access) as key:
                    val, _ = winreg.QueryValueEx(key, "DisplayVersion")
                    if val and str(val).strip():
                        return str(val).strip()
            except OSError:
                continue

    return str(CURRENT_VERSION).strip()


def is_update_available(installed_ver: str, remote_tag: str) -> bool:
    """Devuelve True si la versión remota es estrictamente mayor a la instalada."""
    norm_inst = normalize_version(installed_ver)
    norm_remote = normalize_version(remote_tag)
    return norm_remote > norm_inst


def fetch_latest_release():
    """
    Consulta la API de GitHub para obtener la última release publicada.
    Retorna (tag_name, installer_url, release_name, release_body).
    """
    url = RELEASES_API.format(owner=GITHUB_OWNER, repo=GITHUB_REPO)
    req = urllib.request.Request(
        url,
        headers={
            "User-Agent": f"{APP_NAME}-AutoUpdater/1.1",
            "Accept": "application/vnd.github.v3+json",
        },
    )
    try:
        with urllib.request.urlopen(req, timeout=20) as resp:
            data = json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as err:
        log(f"Error HTTP al consultar GitHub ({err.code}): {err.reason}")
        raise
    except Exception as err:
        log(f"Error de red al consultar GitHub: {err}")
        raise

    tag_name = str(data.get("tag_name", "")).strip()
    release_name = str(data.get("name", "")).strip() or tag_name
    release_body = str(data.get("body", "")).strip()

    installer_url = None
    assets = data.get("assets", [])
    for asset in assets:
        name = str(asset.get("name", "")).lower()
        if name == INSTALLER_ASSET_NAME.lower():
            installer_url = asset.get("browser_download_url")
            break

    return tag_name, installer_url, release_name, release_body


def download_file(url: str, dest_path: Path) -> None:
    """Descarga el archivo desde url al path de destino en trozos de 16KB."""
    log(f"Descargando actualizador desde: {url}")
    req = urllib.request.Request(
        url,
        headers={"User-Agent": f"{APP_NAME}-AutoUpdater/1.1"},
    )
    with urllib.request.urlopen(req, timeout=90) as response:
        with open(dest_path, "wb") as f_out:
            while True:
                chunk = response.read(16384)
                if not chunk:
                    break
                f_out.write(chunk)
    log(f"Descarga completada con éxito en: {dest_path}")


def close_running_instances():
    """Cierra instancias abiertas de artpicst.exe para permitir sobrescritura."""
    try:
        subprocess.run(["taskkill", "/IM", "artpicst.exe", "/T", "/F"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
        time.sleep(1)
    except Exception:
        pass


def update_installed_version_registry_and_file(version_tag: str):
    """Actualiza la versión registrada en el Registro y en version.json."""
    roots = [winreg.HKEY_LOCAL_MACHINE, winreg.HKEY_CURRENT_USER]
    flags = [winreg.KEY_SET_VALUE | winreg.KEY_WOW64_64KEY, winreg.KEY_SET_VALUE]
    for r in roots:
        for f in flags:
            try:
                with winreg.OpenKey(r, INSTALL_REG_KEY, 0, f) as k:
                    winreg.SetValueEx(k, "DisplayVersion", 0, winreg.REG_SZ, version_tag)
            except OSError:
                continue

    # Escribir en version.json si la carpeta existe
    target_dirs = [
        Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / APP_NAME,
        Path(__file__).resolve().parent.parent,
        Path(__file__).resolve().parent.parent / "dist",
    ]
    for d in target_dirs:
        if d.exists() and d.is_dir():
            try:
                vfile = d / "version.json"
                with open(vfile, "w", encoding="utf-8") as f:
                    json.dump({"version": version_tag, "tag": version_tag, "updated_at": time.strftime("%Y-%m-%d %H:%M:%S")}, f, indent=2)
            except Exception:
                pass


def run_installer(installer_path: Path, latest_tag: str) -> bool:
    """Lanza el ejecutable instalador cerrando primero la aplicación."""
    try:
        close_running_instances()
        cmd = [str(installer_path)]
        if INSTALLER_ARGS:
            cmd.extend(shlex.split(INSTALLER_ARGS))

        log(f"Lanzando instalador: {' '.join(cmd)}")
        if AUTO_OPEN_AFTER_INSTALL:
            subprocess.Popen(cmd, shell=False)
        else:
            subprocess.run(cmd, check=False)

        update_installed_version_registry_and_file(latest_tag)
        return True
    except Exception as exc:
        log(f"Error al ejecutar instalador: {exc}")
        return False


def show_message_box(title: str, text: str, style: int = 0) -> int:
    """Muestra un MessageBox nativo de Windows vía ctypes."""
    try:
        ctypes.windll.user32.SetProcessDPIAware()
    except Exception:
        pass
    return ctypes.windll.user32.MessageBoxW(0, text, title, style)


def show_update_prompt(current_ver: str, latest_tag: str, release_name: str) -> bool:
    """Muestra cuadro de diálogo preguntando al usuario si desea actualizar."""
    MB_YESNO = 0x00000004
    MB_ICONQUESTION = 0x00000020
    IDYES = 6

    display_name = release_name if release_name != latest_tag else latest_tag
    msg = (
        f"Hay una nueva versión disponible de {APP_NAME}:\n\n"
        f"• Versión instalada: {current_ver}\n"
        f"• Nueva versión: {display_name}\n\n"
        f"¿Desea descargar e instalar la actualización ahora?"
    )
    res = show_message_box(
        f"Actualización disponible — {APP_NAME}",
        msg,
        MB_YESNO | MB_ICONQUESTION,
    )
    return res == IDYES


def perform_update(silent: bool = False, force: bool = False) -> int:
    """
    Proceso principal de comprobación y actualización.
    Retorna 0 si tuvo éxito / está al día, 1 si hubo error, 2 si hay actualización disponible (en modo comprobación).
    """
    current_ver = get_current_installed_version()
    log(f"Comprobando actualizaciones para {APP_NAME} (versión instalada: {current_ver})...")

    try:
        latest_tag, installer_url, release_name, _ = fetch_latest_release()
    except Exception as exc:
        log(f"No se pudo consultar GitHub: {exc}")
        if not silent:
            show_message_box(
                f"{APP_NAME} — Error de actualización",
                f"No se pudo verificar actualizaciones en este momento.\n\nDetalle: {exc}",
                0x00000010,  # MB_ICONERROR
            )
        return 1

    if not latest_tag:
        log("No se encontró ninguna release publicada.")
        if not silent:
            show_message_box(
                APP_NAME,
                "No hay releases disponibles en el repositorio.",
                0x00000040,  # MB_ICONINFORMATION
            )
        return 0

    log(f"Última versión en GitHub: {latest_tag} ({release_name})")
    update_needed = force or is_update_available(current_ver, latest_tag)

    if not update_needed:
        log(f"La aplicación ya está en su última versión ({current_ver}).")
        if not silent:
            show_message_box(
                f"{APP_NAME} — Actualizaciones",
                f"Ya tienes instalada la versión más reciente de {APP_NAME} ({current_ver}).",
                0x00000040,  # MB_ICONINFORMATION
            )
        return 0

    if not installer_url:
        log(f"La release {latest_tag} no contiene el archivo {INSTALLER_ASSET_NAME}.")
        if not silent:
            show_message_box(
                f"{APP_NAME} — Actualización",
                f"Se encontró la versión {latest_tag}, pero el instalador no está disponible todavía.",
                0x00000030,  # MB_ICONWARNING
            )
        return 0

    if not silent:
        if not show_update_prompt(current_ver, latest_tag, release_name):
            log("El usuario canceló la actualización.")
            return 0

    temp_dir = Path(tempfile.gettempdir())
    installer_file = temp_dir / INSTALLER_ASSET_NAME

    try:
        download_file(installer_url, installer_file)
    except Exception as exc:
        log(f"Error al descargar la actualización: {exc}")
        if not silent:
            show_message_box(
                f"{APP_NAME} — Error",
                f"Error al descargar el archivo de instalación:\n{exc}",
                0x00000010,
            )
        return 1

    if run_installer(installer_file, latest_tag):
        log("Instalador iniciado con éxito.")
        return 0
    else:
        log("No se pudo iniciar el instalador.")
        return 1


def main():
    parser = argparse.ArgumentParser(description=f"Auto Updater de {APP_NAME}")
    parser.add_argument(
        "--check",
        action="store_true",
        help="Solo comprueba si existe una versión más nueva sin descargar",
    )
    parser.add_argument(
        "--silent",
        action="store_true",
        help="Ejecuta la actualización en segundo plano sin cuadros de diálogo",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Fuerza la descarga e instalación independientemente de la versión",
    )
    parser.add_argument(
        "--gui",
        action="store_true",
        help="Muestra diálogo de interfaz siempre (predeterminado)",
    )

    args = parser.parse_args()

    if args.check:
        try:
            current_ver = get_current_installed_version()
            tag, url, name, _ = fetch_latest_release()
            if tag and is_update_available(current_ver, tag):
                log(f"Nueva versión disponible: {tag} (instalada: {current_ver})")
                return 2
            log(f"La aplicación está al día ({current_ver}).")
            return 0
        except Exception as exc:
            log(f"Error al comprobar: {exc}")
            return 1

    return perform_update(silent=args.silent, force=args.force)


if __name__ == "__main__":
    sys.exit(main())
