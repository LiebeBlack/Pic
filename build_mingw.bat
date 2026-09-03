@echo off
setlocal EnableExtensions

cd /d "%~dp0"

echo ========================================
echo ARTPICST - Build System (MinGW Alternative)
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

:: Build main program with MinGW
echo [1/3] Building main program with MinGW...
set "RES_OBJ="
where windres >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    windres artpicst.rc -O coff -o build\artpicst_res.o
    if %ERRORLEVEL% EQU 0 (
        set "RES_OBJ=build\artpicst_res.o"
    ) else (
        echo Warning: windres failed, building without icon/version resources
    )
)
g++ -std=c++17 -O2 -static -static-libgcc -static-libstdc++ -municode -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN -DSTBI_WINDOWS_UTF8 -D_WIN32_WINNT=0x0601 -I. -Iinclude -o build\artpicst.exe src\main.cpp %RES_OBJ% -lgdiplus -luser32 -lkernel32 -lshell32 -lshlwapi -lgdi32 -lmsimg32 -lole32 -loleaut32 -luuid -ldwmapi -lwindowscodecs -lcomdlg32 -mwindows

if %ERRORLEVEL% NEQ 0 (
    echo Error building main program
    exit /b 1
)

:: Build installer with MinGW
echo [2/3] Building installer with MinGW...
cd installer
g++ -std=c++17 -O2 -static -static-libgcc -static-libstdc++ -municode -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0601 -I. -I..\include -o build\artpicst_installer.exe artpicst_installer.cpp -lgdiplus -lshlwapi -lshell32 -lcomctl32 -ldwmapi -luser32 -ladvapi32 -lgdi32 -lole32 -luuid -mwindows
cd ..

if %ERRORLEVEL% NEQ 0 (
    echo Error building installer
    exit /b 1
)

:: Copy files
echo [3/3] Copying files to dist...
copy /y "build\artpicst.exe" "dist\artpicst.exe" >nul
copy /y "installer\build\artpicst_installer.exe" "dist\artpicst_installer.exe" >nul
copy /y "resources\artpicst.ico" "dist\artpicst.ico" >nul
copy /y "version.json" "dist\version.json" >nul

echo.
echo ========================================
echo BUILD SUCCESSFUL (MinGW)
echo ========================================
echo.
echo Files in dist\:
for %%I in (dist\*.exe) do echo %%~nxI: %%~zI bytes

echo.
echo Ready for GitHub release.
exit /b 0
