@echo off
setlocal EnableExtensions

cd /d "%~dp0"

echo Compilando instalador premium con interfaz moderna...

if not exist build mkdir build

:: Baseline SIMD del proyecto: se LEE de tools/build-baseline.json (unica fuente
:: de verdad). Este script ya no escribe el flag ni el define.
set "SIMD_FROM_JSON="
for /f "usebackq delims=" %%i in (`powershell -NoProfile -ExecutionPolicy Bypass -File ..\tools\baseline.ps1 msvc_all`) do set "SIMD_FROM_JSON=%%i"
if not defined SIMD_FROM_JSON (
    echo Error: no se pudo leer el baseline SIMD de tools\build-baseline.json
    exit /b 1
)

:: INCLUDE con la raiz del repo: el .rc localiza el icono (..\resources\artpicst.ico)
set "INCLUDE=%~dp0..;%INCLUDE%"
:: Extension de shell: el .rc la incrusta como RCDATA 206. Se compila aqui (y se
:: copia al payload) para que este script no dependa de un build.ps1 previo.
pushd ..
cl /nologo /EHsc /std:c++latest /O2 /Ob3 /Oi /guard:cf /favor:INTEL64 /Zc:inline %SIMD_FROM_JSON% /utf-8 /W4 /permissive- /wd4324 /Iinclude /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /D_CRT_SECURE_NO_WARNINGS /LD /Fe:"build\artpicst_shell.dll" shell\artpicst_shell.cpp /link /DEF:shell\artpicst_shell.def shlwapi.lib windowscodecs.lib ole32.lib uuid.lib user32.lib gdi32.lib msimg32.lib advapi32.lib /OPT:REF /OPT:ICF /GUARD:CF /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT
if %ERRORLEVEL% NEQ 0 (
    echo Error al compilar la extension de shell
    popd
    exit /b 1
)
if exist resources\app copy /y "build\artpicst_shell.dll" "resources\app\artpicst_shell.dll" >NUL
popd

rc /nologo /fo build\artpicst_installer.res artpicst_installer.rc
if %ERRORLEVEL% NEQ 0 exit /b 1
cl /nologo /EHsc /std:c++latest /O2 /Ob3 /Oi /favor:INTEL64 /Zc:inline %SIMD_FROM_JSON% /utf-8 /W4 /wd4324 /I. /I..\include /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /D_CRT_SECURE_NO_WARNINGS /Fe:"build\artpicst_installer.exe" artpicst_installer.cpp build\artpicst_installer.res /link gdiplus.lib shlwapi.lib shell32.lib comctl32.lib dwmapi.lib user32.lib advapi32.lib gdi32.lib ole32.lib uuid.lib /SUBSYSTEM:WINDOWS "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'" /OPT:REF /OPT:ICF /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT

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