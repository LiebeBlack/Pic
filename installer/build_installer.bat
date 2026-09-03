@echo off
setlocal EnableExtensions

cd /d "%~dp0"

echo Compilando instalador premium con interfaz moderna...

if not exist build mkdir build

cl /nologo /EHsc /std:c++17 /O2 /utf-8 /W4 /I. /I..\include /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /Fe:"build\artpicst_installer.exe" artpicst_installer.cpp /link gdiplus.lib shlwapi.lib shell32.lib comctl32.lib dwmapi.lib user32.lib advapi32.lib gdi32.lib ole32.lib uuid.lib /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF

if %ERRORLEVEL% NEQ 0 (
    echo Error al compilar el instalador
    exit /b 1
)

if not exist ..\dist mkdir ..\dist
copy /y "build\artpicst_installer.exe" "..\dist\artpicst_installer.exe" >nul

echo.
echo Instalador compilado correctamente
echo Ubicación: ..\dist\artpicst_installer.exe

for %%I in (..\dist\artpicst_installer.exe) do echo Tamaño: %%~zI bytes

exit /b 0