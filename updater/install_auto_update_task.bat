@echo off
setlocal EnableExtensions

set "TARGET_EXE=%~1"
if "%TARGET_EXE%"=="" (
    set "TARGET_EXE=%~dp0..\auto_updater.exe"
)

if not exist "%TARGET_EXE%" (
    if exist "%~dp0auto_updater.exe" (
        set "TARGET_EXE=%~dp0auto_updater.exe"
    )
)

if not exist "%TARGET_EXE%" (
    echo [ARTPICST] No se encontro auto_updater.exe en %TARGET_EXE%
    exit /b 1
)

:: Elimina tarea previa si existe
schtasks /Delete /TN "ARTPICST AutoUpdater" /F 2>nul

:: Registra tarea cada 2 días con flag --silent
schtasks /Create /TN "ARTPICST AutoUpdater" /TR "\"%TARGET_EXE%\" --silent" /SC DAILY /MO 2 /ST 09:00 /F /RL LIMITED 2>nul
if %ERRORLEVEL% NEQ 0 (
    schtasks /Create /TN "ARTPICST AutoUpdater" /TR "\"%TARGET_EXE%\" --silent" /SC DAILY /MO 2 /ST 09:00 /F 2>nul
)

echo [ARTPICST] Tarea de actualizacion configurada correctamente.
exit /b 0
