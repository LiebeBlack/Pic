@echo off
setlocal EnableExtensions

cd /d "%~dp0"

echo Compilando actualizador C++ nativo ultra-ligero...

if not exist build mkdir build

cl /nologo /EHsc /std:c++17 /O2 /utf-8 /W4 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /Fe:"build\auto_updater.exe" auto_updater.cpp /link winhttp.lib shlwapi.lib shell32.lib comsuppw.lib user32.lib advapi32.lib /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF

if %ERRORLEVEL% NEQ 0 (
    echo Error al compilar el actualizador
    exit /b 1
)

if not exist ..\dist mkdir ..\dist
copy /y "build\auto_updater.exe" "..\dist\auto_updater.exe" >nul

echo.
echo Actualizador compilado correctamente
echo Tamaño optimizado: menos de 1 MB
echo Ubicación: ..\dist\auto_updater.exe

for %%I in (..\dist\auto_updater.exe) do echo Tamaño real: %%~zI bytes

exit /b 0