@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-local-x64.ps1" -Configuration Release
exit /b %ERRORLEVEL%
