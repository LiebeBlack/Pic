@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /EHsc /std:c++17 /O2 /utf-8 /W4 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /Fe:"build\auto_updater.exe" auto_updater.cpp /link winhttp.lib shlwapi.lib shell32.lib comsuppw.lib user32.lib advapi32.lib /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF
if %ERRORLEVEL% NEQ 0 exit /b 1
if not exist ..\dist mkdir ..\dist
copy /y "build\auto_updater.exe" "..\dist\auto_updater.exe" >nul
echo Actualizador compilado correctamente
for %%I in (..\dist\auto_updater.exe) do echo Tamaño: %%~zI bytes
exit /b 0