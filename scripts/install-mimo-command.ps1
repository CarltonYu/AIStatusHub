param(
    [string] $InstallDir = $(Join-Path $env:USERPROFILE ".local\bin"),
    [string] $RepoRoot = $(Resolve-Path (Join-Path $PSScriptRoot "..")),
    [string] $MimoExe = $(if ($env:MIMO_EXE) { $env:MIMO_EXE } else { "C:\ai\mimo.exe" })
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $MimoExe)) {
    throw "Mimo executable was not found: $MimoExe"
}

$RepoRoot = (Resolve-Path -LiteralPath $RepoRoot).Path
$launcher = Join-Path $RepoRoot "scripts\start-mimo-with-hub.ps1"
if (-not (Test-Path -LiteralPath $launcher)) {
    throw "AIStatusHub Mimo launcher was not found: $launcher"
}

New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
$InstallDir = (Resolve-Path -LiteralPath $InstallDir).Path

$wrapperPath = Join-Path $InstallDir "mimo.cmd"
$directPath = Join-Path $InstallDir "mimo-direct.cmd"

$wrapper = @"
@echo off
setlocal
set "AISTATUSHUB_ROOT=$RepoRoot"
set "MIMO_EXE=$MimoExe"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%AISTATUSHUB_ROOT%\scripts\start-mimo-with-hub.ps1" -Project "%CD%" %*
exit /b %ERRORLEVEL%
"@

$direct = @"
@echo off
"$MimoExe" %*
exit /b %ERRORLEVEL%
"@

$wrapper | Set-Content -LiteralPath $wrapperPath -Encoding ASCII
$direct | Set-Content -LiteralPath $directPath -Encoding ASCII

$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
$pathItems = @()
if ($userPath) {
    $pathItems = $userPath -split ";" | Where-Object { $_ }
}

$alreadyInPath = $false
foreach ($item in $pathItems) {
    if ($item.TrimEnd("\") -ieq $InstallDir.TrimEnd("\")) {
        $alreadyInPath = $true
        break
    }
}

if (-not $alreadyInPath) {
    $newPath = if ($userPath) { "$InstallDir;$userPath" } else { $InstallDir }
    [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
    $env:Path = "$InstallDir;$env:Path"
    Write-Host "Added $InstallDir to the user PATH. Open a new terminal for it to take effect everywhere."
}

& (Join-Path $RepoRoot "scripts\install-mimo-hub-config.ps1") | Out-Host

Write-Host "Installed hub-backed mimo command: $wrapperPath"
Write-Host "Direct bypass command: $directPath"
