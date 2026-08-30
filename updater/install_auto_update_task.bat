@echo off
setlocal

set "TARGET_EXE=%~1"
if "%TARGET_EXE%"=="" (
    set "TARGET_EXE=%~dp0auto_updater.exe"
)

if not exist "%TARGET_EXE%" (
    echo No se encontro auto_updater.exe en %TARGET_EXE%
    exit /b 1
)

schtasks /Delete /TN "ARTPICST AutoUpdater" /F 2>nul
schtasks /Create /TN "ARTPICST AutoUpdater" /TR "\"%TARGET_EXE%\"" /SC DAILY /MO 2 /ST 09:00 /F /RL HIGHEST
schtasks /Query /TN "ARTPICST AutoUpdater" /V

echo Tarea creada correctamente cada 2 dias.
exit /b 0
