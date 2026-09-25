@echo off
setlocal EnableExtensions

cd /d "%~dp0"

echo ========================================
echo ARTPICST - Build System (MinGW Alternative)
echo Orden: recursos -^> visor -^> updater -^> payload -^> instalador
echo ========================================
echo.

:: Check for MinGW
where g++ >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo MinGW not found. Installing via MSYS2...
    echo Please install MSYS2 from https://www.msys2.org/
    echo Then run: pacman -S mingw-w64-x86_64-gcc
    exit /b 1
)

echo MinGW found
echo.

:: Create directories
if not exist build mkdir build
if not exist dist mkdir dist
if not exist installer\build mkdir installer\build
if not exist updater\build mkdir updater\build

:: Preprocesador explicito: en varios entornos MinGW windres no encuentra su
:: propio cc1 y falla con "cannot execute 'cc1'", dejando el .exe sin icono.
set "WINDRES_PP=gcc -E -xc -DRC_INVOKED"

:: [1/5] Resources (viewer + updater + installer)
echo [1/5] Compiling resources with windres...
set "RES_OBJ="
where windres >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    windres artpicst.rc -O coff -o build\artpicst_res.o -I. --preprocessor="%WINDRES_PP%"
    if %ERRORLEVEL% EQU 0 (
        set "RES_OBJ=build\artpicst_res.o"
    ) else (
        echo Warning: windres failed, building without icon/version resources
    )
)

set "UPDATER_RES_OBJ="
pushd updater
windres artpicst_updater.rc -O coff -o ..\build\artpicst_updater_res.o -I. --preprocessor="%WINDRES_PP%"
if %ERRORLEVEL% EQU 0 (
    set "UPDATER_RES_OBJ=..\build\artpicst_updater_res.o"
) else (
    echo Warning: windres failed for updater resources
)
popd

:: [2/5] Viewer
echo [2/5] Building main program with MinGW...
g++ -std=c++23 -O3 -funroll-loops -fno-math-errno -static -static-libgcc -static-libstdc++ -municode -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN -DSTBI_WINDOWS_UTF8 -D_WIN32_WINNT=0x0601 -I. -Iinclude -o build\artpicst.exe src\main.cpp %RES_OBJ% -lgdiplus -luser32 -lkernel32 -lshell32 -lshlwapi -lgdi32 -lmsimg32 -lole32 -loleaut32 -luuid -ldwmapi -lwindowscodecs -lcomdlg32 -ld2d1 -ldwrite -mwindows

if %ERRORLEVEL% NEQ 0 (
    echo Error building main program
    exit /b 1
)

:: [3/5] Updater (needs winhttp + installer/version.hpp)
echo [3/5] Building updater with MinGW...
pushd updater
g++ -std=c++23 -O2 -static -static-libgcc -static-libstdc++ -municode -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0601 -I. -I..\installer -o ..\build\artpicst_updater.exe artpicst_updater.cpp %UPDATER_RES_OBJ% -lwinhttp -lgdiplus -lshell32 -lshlwapi -luser32 -ladvapi32 -lgdi32 -lole32 -luuid -ldwmapi -mwindows
if %ERRORLEVEL% NEQ 0 (
    echo Error building updater
    popd
    exit /b 1
)
popd

:: Quality gate: updater selftest
build\artpicst_updater.exe --selftest
if %ERRORLEVEL% NEQ 0 (
    echo Updater selftest failed
    exit /b 1
)
echo Updater selftest OK

:: [4/5] Self-contained payload
echo [4/5] Preparing self-contained payload (resources\app\)...
python scripts\collect_app_payload.py --root .
if %ERRORLEVEL% NEQ 0 (
    echo Error preparing payload
    exit /b 1
)

:: [5/5] Self-contained installer (resources AFTER payload: embeds resources\app\)
echo [5/5] Building self-contained installer with MinGW...
pushd installer
if not exist build mkdir build
set "INSTALLER_RES_OBJ="
windres artpicst_installer.rc -O coff -o ..\build\artpicst_installer_res.o -I. --preprocessor="%WINDRES_PP%"
if %ERRORLEVEL% EQU 0 (
    set "INSTALLER_RES_OBJ=..\build\artpicst_installer_res.o"
) else (
    echo Warning: windres failed for installer resources
)
g++ -std=c++23 -O2 -static -static-libgcc -static-libstdc++ -municode -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0601 -I. -I..\include -o build\artpicst_installer.exe artpicst_installer.cpp %INSTALLER_RES_OBJ% -lgdiplus -lshlwapi -lshell32 -lcomctl32 -ldwmapi -luser32 -ladvapi32 -lgdi32 -lole32 -luuid -mwindows
if %ERRORLEVEL% NEQ 0 (
    echo Error building installer
    popd
    exit /b 1
)
popd

:: Copy files
echo Copying files to dist...
copy /y "build\artpicst.exe" "dist\artpicst.exe" >nul
copy /y "build\artpicst_updater.exe" "dist\artpicst_updater.exe" >nul
copy /y "installer\build\artpicst_installer.exe" "dist\artpicst_installer.exe" >nul
copy /y "resources\artpicst.ico" "dist\artpicst.ico" >nul
copy /y "version.json" "dist\version.json" >nul
copy /y "README.md" "dist\README.md" >nul

echo.
echo ========================================
echo BUILD SUCCESSFUL (MinGW)
echo ========================================
echo.
echo Files in dist\:
for %%I in (dist\*.exe) do echo %%~nxI: %%~zI bytes

echo.
echo Note: the official release asset is 'artpicst-installer.exe'
echo (copy/rename dist\artpicst_installer.exe when publishing).
echo.
echo Ready for GitHub release.
exit /b 0
