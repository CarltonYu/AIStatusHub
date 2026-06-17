param(
    [string] $InstallDir = $(Join-Path $env:USERPROFILE ".local\bin"),
    [string] $RepoRoot = $(Resolve-Path (Join-Path $PSScriptRoot "..")),
    [switch] $NoBuild,
    [switch] $NoPathUpdate
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

if (-not $env:USERPROFILE) {
    throw "USERPROFILE is not set; pass -InstallDir explicitly."
}

$RepoRoot = (Resolve-Path -LiteralPath $RepoRoot).Path
$toolDir = Join-Path $RepoRoot "tools\duo-console"
$manifest = Join-Path $toolDir "Cargo.toml"
if (-not (Test-Path -LiteralPath $manifest)) {
    throw "duo-console Cargo.toml was not found: $manifest"
}

if (-not $NoBuild) {
    $cargo = Get-Command cargo -ErrorAction SilentlyContinue
    if (-not $cargo) {
        throw "cargo is not installed or not on PATH. Install Rust first, then rerun this script."
    }

    Push-Location -LiteralPath $toolDir
    try {
        & $cargo.Source build --release
        if ($LASTEXITCODE -ne 0) {
            throw "cargo build --release failed with exit code $LASTEXITCODE"
        }
    } finally {
        Pop-Location
    }
}

$sourceExe = Join-Path $toolDir "target\release\duo-console.exe"
if (-not (Test-Path -LiteralPath $sourceExe)) {
    throw "duo-console.exe was not found: $sourceExe"
}

New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
$InstallDir = (Resolve-Path -LiteralPath $InstallDir).Path
$installedExe = Join-Path $InstallDir "duo-console.exe"
Copy-Item -LiteralPath $sourceExe -Destination $installedExe -Force

if (-not $NoPathUpdate) {
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
}

Write-Host "Installed duo-console command: $installedExe"
& $installedExe --version
