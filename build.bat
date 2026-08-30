@echo off
setlocal EnableExtensions

cd /d "%~dp0"

if not exist resources mkdir resources
if not exist resources mkdir resources
if not exist "resources\artpicst.ico" (
    if exist "scripts\generate_icon.py" (
        python scripts\generate_icon.py
    )
)

where cmake >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    if not exist build mkdir build
    cmake -S . -B build
    if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

    cmake --build build --config Release
    if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

    if not exist dist mkdir dist
    cmake --install build --config Release --prefix "%CD%\dist"
    if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

    echo.
    echo ARTPICST compilado y empaquetado correctamente con CMake.
    echo Directorio de distribucion: %CD%\dist
    if exist "%CD%\dist\artpicst.exe" (
        echo Ejecutable: %CD%\dist\artpicst.exe
    )
    exit /b 0
)

where cl >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo No se encontro CMake ni un compilador de Visual Studio en PATH.
    echo Instala Visual Studio Build Tools con C++.
    echo Tambien puedes compilar el updater con: python updater\build_exe.py
    exit /b 1
)

if not exist build mkdir build

set "OUTPUT=build\artpicst.exe"
cl /nologo /EHsc /std:c++17 /O2 /utf-8 /W4 /I. /Iinclude /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DSTBI_WINDOWS_UTF8 /D_WIN32_WINNT=0x0601 /Fe:"%OUTPUT%" src\main.cpp artpicst.manifest artpicst.rc /link gdiplus.lib user32.lib kernel32.lib shell32.lib shlwapi.lib gdi32.lib msimg32.lib ole32.lib oleaut32.lib uuid.lib dwmapi.lib windowscodecs.lib comdlg32.lib /SUBSYSTEM:WINDOWS
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

if not exist dist mkdir dist
copy /y "build\artpicst.exe" "dist\artpicst.exe" >nul
copy /y "resources\artpicst.ico" "dist\artpicst.ico" >nul

echo.
echo ARTPICST compilado y empaquetado correctamente con cl.exe.
echo Directorio de distribucion: %CD%\dist
exit /b 0
