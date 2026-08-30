APP_NAME = "ARTPICST"
GITHUB_OWNER = "LiebeBlack"
GITHUB_REPO = "Pic"
RELEASES_API = "https://api.github.com/repos/{owner}/{repo}/releases/latest"
INSTALLER_ASSET_NAME = "artpicst-installer.exe"

# En este proyecto las releases publicadas son del estilo: auto-23
# Por eso la version instalada debe ser comparada como numero entero del build.
CURRENT_VERSION = "22"
INSTALL_REG_KEY = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\ARTPICST"
INSTALLER_ARGS = "/S"
AUTO_OPEN_AFTER_INSTALL = True

# Regla de comparacion: si la tag del release es mayor que la version actual, hay actualizacion.
# Ejemplos validos: auto-23, auto-22, v1.2.3, 1.2.3
