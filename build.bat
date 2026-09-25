@echo off
setlocal EnableExtensions DisableDelayedExpansion

cd /d "%~dp0"

echo ========================================
echo ARTPICST - Sistema de Compilacion Integrado
echo Orden: recursos -^> visor -^> updater -^> payload -^> instalador
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
if not exist updater\build mkdir updater\build

echo [1/5] Compilando recursos (visor y updater)...
rc /nologo /fo build\artpicst.res artpicst.rc
if %ERRORLEVEL% NEQ 0 (
    echo Error al compilar los recursos del visor
    exit /b 1
)
cd updater
rc /nologo /fo ..\build\artpicst_updater.res artpicst_updater.rc
if %ERRORLEVEL% NEQ 0 (
    echo Error al compilar los recursos del updater
    cd ..
    exit /b 1
)
cd ..

echo [2/5] Compilando visor (artpicst.exe)...
cl /nologo /EHsc /std:c++latest /O2 /Ob3 /Oi /GL /Gy /utf-8 /W4 /wd4324 /I. /Iinclude /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DSTBI_WINDOWS_UTF8 /D_WIN32_WINNT=0x0601 /D_CRT_SECURE_NO_WARNINGS /Fe:"build\artpicst.exe" src\main.cpp build\artpicst.res /link gdiplus.lib user32.lib kernel32.lib shell32.lib shlwapi.lib gdi32.lib msimg32.lib ole32.lib oleaut32.lib uuid.lib dwmapi.lib windowscodecs.lib comdlg32.lib d2d1.lib dwrite.lib advapi32.lib /MANIFEST:EMBED /MANIFESTINPUT:artpicst.manifest /SUBSYSTEM:WINDOWS /LTCG /OPT:REF /OPT:ICF

if %ERRORLEVEL% NEQ 0 (
    echo Error al compilar el visor
    exit /b 1
)

echo [3/5] Compilando updater (artpicst_updater.exe)...
cd updater
cl /nologo /EHsc /std:c++latest /O2 /Ob3 /Oi /utf-8 /W4 /wd4324 /I. /I..\installer /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /D_CRT_SECURE_NO_WARNINGS /Fe:"..\build\artpicst_updater.exe" artpicst_updater.cpp ..\build\artpicst_updater.res /link winhttp.lib gdiplus.lib shell32.lib shlwapi.lib user32.lib advapi32.lib gdi32.lib ole32.lib uuid.lib dwmapi.lib /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF
if %ERRORLEVEL% NEQ 0 (
    echo Error al compilar el updater
    cd ..
    exit /b 1
)
cd ..

:: Gate de calidad: selftest del updater (JSON, SHA-256, versiones)
build\artpicst_updater.exe --selftest
if %ERRORLEVEL% NEQ 0 (
    echo El selftest del updater ha fallado
    exit /b 1
)
echo Selftest del updater OK

echo [4/5] Preparando payload autocontenido (resources\app\)...
python scripts\collect_app_payload.py --root .
if %ERRORLEVEL% NEQ 0 (
    echo Error al preparar el payload autocontenido
    exit /b 1
)

echo [5/5] Compilando instalador autocontenido (artpicst_installer.exe)...
cd installer
:: Recursos DESPUES del payload: el .rc incrusta resources\app\ (payload)
rc /nologo /fo ..\build\artpicst_installer.res artpicst_installer.rc
if %ERRORLEVEL% NEQ 0 (
    echo Error al compilar los recursos del instalador
    cd ..
    exit /b 1
)
:: INCLUDE con la raiz del repo: el .rc localiza el icono (resources\artpicst.ico)
set "INCLUDE=%~dp0;%INCLUDE%"
cl /nologo /EHsc /std:c++latest /O2 /Ob3 /Oi /utf-8 /W4 /wd4324 /I. /I..\include /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /D_CRT_SECURE_NO_WARNINGS /Fe:"build\artpicst_installer.exe" artpicst_installer.cpp ..\build\artpicst_installer.res /link gdiplus.lib shlwapi.lib shell32.lib comctl32.lib dwmapi.lib user32.lib advapi32.lib gdi32.lib ole32.lib uuid.lib /SUBSYSTEM:WINDOWS "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'" /OPT:REF /OPT:ICF

if %ERRORLEVEL% NEQ 0 (
    echo Error al compilar el instalador
    cd ..
    exit /b 1
)

cd ..

echo Copiando archivos a directorio de distribucion...
copy /y "build\artpicst.exe" "dist\artpicst.exe" >nul
copy /y "build\artpicst_updater.exe" "dist\artpicst_updater.exe" >nul
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
echo   - artpicst_updater.exe (Modulo de actualizacion)
echo   - artpicst_installer.exe (Instalador autocontenido)
echo   - artpicst.ico (Icono)
echo   - version.json (Version info)
echo   - README.md (Documentacion)
echo.

:: Mostrar tamano de archivos
for %%I in (dist\artpicst.exe) do echo Programa principal: %%~zI bytes
for %%I in (dist\artpicst_updater.exe) do echo Updater: %%~zI bytes
for %%I in (dist\artpicst_installer.exe) do echo Instalador: %%~zI bytes

echo.
echo Nota: el asset oficial de release es 'artpicst-installer.exe'
echo (copia/renombra dist\artpicst_installer.exe al publicar).
echo.
echo Sistema listo para distribucion en GitHub sin errores.
exit /b 0
