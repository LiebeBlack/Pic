# Configuración centralizada de ARTPICST y su Updater

APP_NAME = "ARTPICST"
GITHUB_OWNER = "LiebeBlack"
GITHUB_REPO = "Pic"
RELEASES_API = "https://api.github.com/repos/{owner}/{repo}/releases/latest"
INSTALLER_ASSET_NAME = "artpicst-installer.exe"
ZIP_ASSET_NAME = "artpicst-portable.zip"

# Versión base del cliente instalada (soporta números enteros, semver tipo 1.1.0 o tags auto-28)
CURRENT_VERSION = "auto-28"

# Claves de registro de Windows para detección de desinstalador
INSTALL_REG_KEY = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\ARTPICST"
INSTALLER_ARGS = "/S"
AUTO_OPEN_AFTER_INSTALL = True
