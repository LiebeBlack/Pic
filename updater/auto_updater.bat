@echo off
setlocal EnableExtensions DisableDelayedExpansion

set "APP_NAME=ARTPICST"
set "GITHUB_OWNER=LiebeBlack"
set "GITHUB_REPO=Pic"
set "INSTALLER_NAME=artpicst-installer.exe"
set "REG_KEY=Software\Microsoft\Windows\CurrentVersion\Uninstall\ARTPICST"
set "CURRENT_VERSION=auto-28"

echo [%APP_NAME% Updater] Iniciando comprobacion de actualizaciones...

:: Obtener version instalada desde registro
set "INSTALLED_VERSION=%CURRENT_VERSION%"
for /f "tokens=2*" %%A in ('reg query "HKLM\%REG_KEY%" /v DisplayVersion 2^>nul') do set "INSTALLED_VERSION=%%B"
for /f "tokens=2*" %%A in ('reg query "HKCU\%REG_KEY%" /v DisplayVersion 2^>nul') do set "INSTALLED_VERSION=%%B"

echo Version instalada: %INSTALLED_VERSION%

:: Usar PowerShell para descargar JSON de GitHub
set "API_URL=https://api.github.com/repos/%GITHUB_OWNER%/%GITHUB_REPO%/releases/latest"

for /f "delims=" %%i in ('powershell -Command "& { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; $response = Invoke-WebRequest -Uri '%API_URL%' -UseBasicParsing; $response.Content }"') do set "JSON_RESPONSE=%%i"

:: Extraer tag_name con PowerShell (más robusto)
for /f "delims=" %%i in ('powershell -Command "& { $json = '%JSON_RESPONSE%' | ConvertFrom-Json; $json.tag_name }"') do set "LATEST_TAG=%%i"

if "%LATEST_TAG%"=="" (
    echo No se pudo obtener la ultima version
    exit /b 1
)

echo Ultima version en GitHub: %LATEST_TAG%

:: Comparar versiones (simplificada - solo numeros)
set "INSTALLED_NUM=%INSTALLED_VERSION:auto-=%"
set "LATEST_NUM=%LATEST_TAG:auto-=%"

if %LATEST_NUM% LEQ %INSTALLED_NUM% (
    echo La aplicacion esta actualizada
    msg * %APP_NAME% esta actualizado a la version %INSTALLED_VERSION%
    exit /b 0
)

:: Preguntar al usuario
set "MSG=Se encontro una nueva version: %LATEST_TAG%^n^nVersion actual: %INSTALLED_VERSION%^nNueva version: %LATEST_TAG%^n^n¿Desea descargar e instalar la actualizacion?"
msg * /time:30 %MSG%

:: Descargar instalador
set "TEMP_DIR=%TEMP%"
set "INSTALLER_PATH=%TEMP_DIR%\%INSTALLER_NAME%"

echo Descargando instalador...
powershell -Command "& { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; $url = 'https://github.com/%GITHUB_OWNER%/%GITHUB_REPO%/releases/latest/download/%INSTALLER_NAME%'; $output = '%INSTALLER_PATH%'; Invoke-WebRequest -Uri $url -OutFile $output }"

if not exist "%INSTALLER_PATH%" (
    echo Error al descargar el instalador
    msg * Error al descargar el instalador
    exit /b 1
)

echo Instalador descargado correctamente

:: Cerrar instancias en ejecucion
taskkill /IM artpicst.exe /T /F >nul 2>&1
timeout /t 2 /nobreak >nul

:: Ejecutar instalador
echo Iniciando instalador...
start "" "%INSTALLER_PATH%" /S

echo Actualizacion iniciada
exit /b 0