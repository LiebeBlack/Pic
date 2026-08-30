@echo off
setlocal EnableExtensions

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
if not exist updater\build mkdir updater\build

:: Build main program with MinGW
echo [1/4] Building main program with MinGW...
g++ -std=c++17 -O2 -static -static-libgcc -static-libstdc++ -municode -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN -DSTBI_WINDOWS_UTF8 -D_WIN32_WINNT=0x0601 -I. -Iinclude -o build\artpicst.exe src\main.cpp -lgdiplus -luser32 -lkernel32 -lshell32 -lshlwapi -lgdi32 -lmsimg32 -lole32 -loleaut32 -luuid -ldwmapi -lwindowscodecs -lcomdlg32 -mwindows

if %ERRORLEVEL% NEQ 0 (
    echo Error building main program
    exit /b 1
)

:: Build installer with MinGW
echo [2/4] Building installer with MinGW...
cd installer
g++ -std=c++17 -O2 -static -static-libgcc -static-libstdc++ -municode -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0601 -I. -I..\include -o build\artpicst_installer.exe artpicst_installer.cpp -lgdiplus -lshlwapi -lshell32 -lcomctl32 -ldwmapi -luser32 -ladvapi32 -mwindows
cd ..

if %ERRORLEVEL% NEQ 0 (
    echo Error building installer
    exit /b 1
)

:: Build updater with MinGW
echo [3/4] Building updater with MinGW...
cd updater
g++ -std=c++17 -O2 -static -static-libgcc -static-libstdc++ -municode -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0601 -o build\auto_updater.exe auto_updater_simple.cpp -lwinhttp -lshlwapi -lshell32 -luser32 -ladvapi32 -mwindows
cd ..

if %ERRORLEVEL% NEQ 0 (
    echo Error building updater
    exit /b 1
)

:: Copy files
echo [4/4] Copying files to dist...
copy /y "build\artpicst.exe" "dist\artpicst.exe" >nul
copy /y "installer\build\artpicst_installer.exe" "dist\artpicst_installer.exe" >nul
copy /y "updater\build\auto_updater.exe" "dist\auto_updater.exe" >nul
copy /y "resources\artpicst.ico" "dist\artpicst.ico" >nul
copy /y "version.json" "dist\version.json" >nul

echo @echo off > "dist\auto_updater.bat"
echo setlocal EnableExtensions >> "dist\auto_updater.bat"
echo cd /d "%%~dp0" >> "dist\auto_updater.bat"
echo if exist "auto_updater.exe" start "" "auto_updater.exe" --gui >> "dist\auto_updater.bat"

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
