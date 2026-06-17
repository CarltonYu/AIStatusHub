@echo off
setlocal
set "ROOT=%~dp0"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\status-ai-hub-service.ps1" %*
set "EXITCODE=%ERRORLEVEL%"

echo.
if not "%AI_STATUS_HUB_NO_PAUSE%"=="1" pause
exit /b %EXITCODE%
