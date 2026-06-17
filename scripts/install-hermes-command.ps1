param(
    [string] $InstallDir = $(Join-Path $env:USERPROFILE ".local\bin"),
    [string] $Distro = $(if ($env:HERMES_WSL_DISTRO) { $env:HERMES_WSL_DISTRO } else { "HermesUbuntu" }),
    [string] $HermesBin = $(if ($env:HERMES_WSL_BIN) { $env:HERMES_WSL_BIN } else { "/home/hermes/.local/bin/hermes" })
)

$ErrorActionPreference = "Stop"

function Normalize-PathItem {
    param([string] $Value)

    $expanded = [Environment]::ExpandEnvironmentVariables($Value).Trim()
    if ($expanded.EndsWith("\")) {
        $expanded = $expanded.TrimEnd("\")
    }

    try {
        return [System.IO.Path]::GetFullPath($expanded).TrimEnd("\")
    } catch {
        return $expanded
    }
}

function Write-CmdFile {
    param(
        [string] $Path,
        [string] $Content
    )

    $normalized = ($Content.Trim("`r", "`n") -replace "\r?\n", "`r`n") + "`r`n"
    Set-Content -LiteralPath $Path -Value $normalized -Encoding ASCII -NoNewline
}

if (-not $env:USERPROFILE) {
    throw "USERPROFILE is not set; pass -InstallDir explicitly."
}

$wsl = Get-Command wsl.exe -ErrorAction SilentlyContinue
if (-not $wsl) {
    throw "wsl.exe was not found on PATH."
}

$distroNames = & $wsl.Source -l -q
if ($LASTEXITCODE -ne 0) {
    throw "Unable to list WSL distros."
}

$distroFound = $false
foreach ($name in $distroNames) {
    if ($name.Trim([char]0).Trim() -eq $Distro) {
        $distroFound = $true
        break
    }
}

if (-not $distroFound) {
    throw "WSL distro '$Distro' was not found."
}

& $wsl.Source -d $Distro -- test -x $HermesBin
if ($LASTEXITCODE -ne 0) {
    throw "Hermes binary was not found or is not executable in '$Distro': $HermesBin"
}

New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
$InstallDir = (Resolve-Path -LiteralPath $InstallDir).Path

$hermesPath = Join-Path $InstallDir "hermes.cmd"
$hermesTuiPath = Join-Path $InstallDir "hermes-tui.cmd"

$hermesWrapper = @"
@echo off
setlocal
if not defined HERMES_WSL_DISTRO set "HERMES_WSL_DISTRO=$Distro"
if not defined HERMES_WSL_BIN set "HERMES_WSL_BIN=$HermesBin"
wsl.exe -d %HERMES_WSL_DISTRO% --cd "%CD%" -- %HERMES_WSL_BIN% %*
exit /b %ERRORLEVEL%
"@

$hermesTuiWrapper = @"
@echo off
setlocal
if not defined HERMES_WSL_DISTRO set "HERMES_WSL_DISTRO=$Distro"
if not defined HERMES_WSL_BIN set "HERMES_WSL_BIN=$HermesBin"
wsl.exe -d %HERMES_WSL_DISTRO% --cd "%CD%" -- %HERMES_WSL_BIN% --tui %*
exit /b %ERRORLEVEL%
"@

Write-CmdFile -Path $hermesPath -Content $hermesWrapper
Write-CmdFile -Path $hermesTuiPath -Content $hermesTuiWrapper

$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
$pathItems = @()
if ($userPath) {
    $pathItems = $userPath -split ";" | Where-Object { $_ }
}

$installDirNorm = Normalize-PathItem $InstallDir
$alreadyInPath = $false
foreach ($item in $pathItems) {
    if ((Normalize-PathItem $item) -ieq $installDirNorm) {
        $alreadyInPath = $true
        break
    }
}

if (-not $alreadyInPath) {
    $newPath = if ($userPath) { "$InstallDir;$userPath" } else { $InstallDir }
    [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
    $env:Path = "$InstallDir;$env:Path"
    Write-Host "Added $InstallDir to the user PATH. Open a new terminal for it to take effect everywhere."
} else {
    Write-Host "$InstallDir is already in the user PATH."
}

Write-Host "Installed Hermes command: $hermesPath"
Write-Host "Installed Hermes TUI command: $hermesTuiPath"
& (Join-Path $InstallDir "hermes.cmd") --version
