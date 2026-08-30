# ARTPICST Updater

Actualizador independiente y seguro para ARTPICST. Comprueba la última versión en GitHub Releases, descarga el instalador y lo ejecuta de forma silenciosa o con confirmación de usuario.

## Características

- **Cero dependencias externas**: Usa la biblioteca estándar de Python (`urllib.request`, `winreg`, `ctypes`, etc.).
- **Detección inteligente de versiones**: Compatible con tags `auto-XX`, `vX.Y.Z`, `X.Y.Z` y números de build.
- **Modos de ejecución**:
  - `python auto_updater.py` o doble clic: Modo interactivo con interfaz nativa de Windows.
  - `python auto_updater.py --check`: Comprueba si hay actualizaciones sin descargar.
  - `python auto_updater.py --silent`: Actualización en segundo plano para tareas programadas.
  - `python auto_updater.py --force`: Fuerza descarga e instalación.
- **Empaquetado**: `build_exe.py` compila `dist/auto_updater.exe` usando PyInstaller.

## Configuración (`config.py`)

- `GITHUB_OWNER`: Usuario u organización de GitHub (`LiebeBlack`).
- `GITHUB_REPO`: Nombre del repositorio (`Pic`).
- `INSTALLER_ASSET_NAME`: Nombre del instalador generado (`artpicst-installer.exe`).
- `CURRENT_VERSION`: Versión actual del software.

## Compilar el ejecutable autónomo

```bat
python build_exe.py
```
El ejecutable resultante quedará en `dist\auto_updater.exe`.
