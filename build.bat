@echo off
setlocal EnableExtensions DisableDelayedExpansion

cd /d "%~dp0"

echo ========================================
echo ARTPICST - Sistema de Compilacion Integrado
echo ========================================
echo.

:: Configurar entorno de Visual Studio
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo Error: No se encontro Visual Studio 2022
    echo Intentando con versiones alternativas...
    call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
    if %ERRORLEVEL% NEQ 0 (
        echo Error: No se encontro Visual Studio
        echo Por favor instala Visual Studio Build Tools con C++
        exit /b 1
    )
)

:: Crear directorios necesarios
if not exist build mkdir build
if not exist dist mkdir dist
if not exist installer\build mkdir installer\build

echo [1/3] Compilando programa principal...
cl /nologo /EHsc /std:c++17 /O2 /utf-8 /W4 /I. /Iinclude /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DSTBI_WINDOWS_UTF8 /D_WIN32_WINNT=0x0601 /Fe:"build\artpicst.exe" src\main.cpp artpicst.manifest artpicst.rc /link gdiplus.lib user32.lib kernel32.lib shell32.lib shlwapi.lib gdi32.lib msimg32.lib ole32.lib oleaut32.lib uuid.lib dwmapi.lib windowscodecs.lib comdlg32.lib /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF

if %ERRORLEVEL% NEQ 0 (
    echo Error al compilar el programa principal
    exit /b 1
)

echo [2/3] Compilando instalador premium...
cd installer
cl /nologo /EHsc /std:c++17 /O2 /utf-8 /W4 /I. /I..\include /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /Fe:"build\artpicst_installer.exe" artpicst_installer.cpp /link gdiplus.lib shlwapi.lib shell32.lib comctl32.lib dwmapi.lib user32.lib advapi32.lib gdi32.lib ole32.lib uuid.lib /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF

if %ERRORLEVEL% NEQ 0 (
    echo Error al compilar el instalador
    cd ..
    exit /b 1
)

cd ..

echo [3/3] Copiando archivos a directorio de distribucion...
copy /y "build\artpicst.exe" "dist\artpicst.exe" >nul
copy /y "installer\build\artpicst_installer.exe" "dist\artpicst_installer.exe" >nul
copy /y "resources\artpicst.ico" "dist\artpicst.ico" >nul
copy /y "version.json" "dist\version.json" >nul
copy /y "README.md" "dist\README.md" >nul

echo.
echo ========================================
echo COMPILACION COMPLETADA EXITOSAMENTE
echo ========================================
echo.
echo Archivos generados en directorio dist\:
echo   - artpicst.exe (Programa principal)
echo   - artpicst_installer.exe (Instalador premium)
echo   - artpicst.ico (Icono)
echo   - version.json (Version info)
echo   - README.md (Documentacion)
echo.

:: Mostrar tamaños de archivos
for %%I in (dist\artpicst.exe) do echo Programa principal: %%~zI bytes
for %%I in (dist\artpicst_installer.exe) do echo Instalador: %%~zI bytes

echo.
echo Sistema listo para distribucion en GitHub sin errores.
exit /b 0
