@echo off
echo Testing build configuration...

echo Checking source files...
if exist "src\main.cpp" echo [OK] src\main.cpp
if exist "installer\artpicst_installer.cpp" echo [OK] installer\artpicst_installer.cpp  

if exist "resources\artpicst.ico" echo [OK] resources\artpicst.ico
if exist "artpicst.rc" echo [OK] artpicst.rc
if exist "artpicst.manifest" echo [OK] artpicst.manifest
if exist "version.json" echo [OK] version.json

echo.
echo Checking build scripts...
if exist "build.bat" echo [OK] build.bat
if exist "build.ps1" echo [OK] build.ps1
if exist "build_mingw.bat" echo [OK] build_mingw.bat

echo.
echo Checking GitHub Actions...
if exist ".github\workflows\build.yml" echo [OK] .github\workflows\build.yml

echo.
echo Configuration check complete.
echo Ready for GitHub Actions build.
pause