@echo off
setlocal
set "ROOT=%~dp0"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\install-ai-hub-service.ps1" -StartNow %*
set "EXITCODE=%ERRORLEVEL%"

echo.
if not "%AI_STATUS_HUB_NO_PAUSE%"=="1" pause
exit /b %EXITCODE%
