@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0start.ps1" -ListenHost 0.0.0.0 -OpenWebsite %*
if errorlevel 1 pause
