@echo off
setlocal EnableExtensions

cd /d "%~dp0"

where cmake >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    if not exist build mkdir build
    cmake -S . -B build
    if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

    cmake --build build --config Release
    if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

    echo.
    echo ARTPICST compilado correctamente con CMake.
    if exist "build\bin\artpicst.exe" (
        echo Ejecutable: build\bin\artpicst.exe
    ) else (
        echo Busca artpicst.exe dentro de la carpeta build\
    )
    exit /b 0
)

where cl >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo No se encontro CMake ni un compilador de Visual Studio en PATH.
    echo Instala Visual Studio 2022 Build Tools con "Desktop development with C++".
    echo Tambien puedes instalar CMake desde: https://cmake.org/download/
    echo Luego vuelve a ejecutar este script.
    exit /b 1
)

if not exist build mkdir build

set "OUTPUT=build\artpicst.exe"
cl /nologo /EHsc /std:c++17 /O2 /utf-8 /Iinclude /Fe:"%OUTPUT%" src\main.cpp /link gdiplus.lib user32.lib kernel32.lib shell32.lib gdi32.lib msimg32.lib ole32.lib oleaut32.lib uuid.lib dwmapi.lib windowscodecs.lib comdlg32.lib /SUBSYSTEM:WINDOWS
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

echo.
echo ARTPICST compilado correctamente con cl.exe.
echo Ejecutable: %OUTPUT%
exit /b 0
