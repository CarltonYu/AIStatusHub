@echo off
setlocal
set "ROOT=%~dp0"

pushd "%ROOT%" >nul
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\start-mimo-with-hub.ps1" %*
set "EXITCODE=%ERRORLEVEL%"
popd >nul
exit /b %EXITCODE%
