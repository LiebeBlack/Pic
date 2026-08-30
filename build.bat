@echo off
setlocal EnableExtensions

cd /d "%~dp0"

if not exist resources mkdir resources
if not exist "resources\artpicst.ico" (
    echo Generando icono del programa...
    python -c "from PIL import Image, ImageDraw, ImageFont; sizes=[16,24,32,48,64,128,256]; icons=[]; [icons.append(Image.new('RGBA',(s,s),(0,0,0,0))) for s in sizes]; 
    for s in sizes:
        im = Image.new('RGBA', (s, s), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        pad = max(1, int(s * 0.12))
        d.rounded_rectangle((pad, pad, s - pad, s - pad), radius=max(4, int(s * 0.16)), fill=(15,23,34,255))
        d.rounded_rectangle((max(1, int(s * 0.22)), max(1, int(s * 0.22)), s - max(1, int(s * 0.22)), s - max(1, int(s * 0.22))), radius=max(4, int(s * 0.12)), fill=(27,34,42,255))
        try:
            font = ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf', max(10, int(s * 0.60)))
        except Exception:
            font = ImageFont.load_default()
        bbox = d.textbbox((0,0), 'A', font=font)
        tw = bbox[2] - bbox[0]; th = bbox[3] - bbox[1]
        x = (s - tw) / 2; y = (s - th) / 2 - int(s * 0.04)
        d.text((x, y), 'A', font=font, fill=(102,176,255,255))
        d.rounded_rectangle((int(s * 0.30), int(s * 0.34), int(s * 0.68), int(s * 0.72)), radius=max(2, int(s * 0.06)), fill=(255,255,255,28))
        icons[sizes.index(s)] = im
    icons[0].save('resources/artpicst.ico', format='ICO', sizes=[(s,s) for s in sizes])"
    if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%
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
