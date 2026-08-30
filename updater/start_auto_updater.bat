@echo off
setlocal
cd /d "%~dp0"
python auto_updater.py
if errorlevel 1 exit /b 1
exit /b 0
