param(
    [string] $Config = $env:AI_STATUS_HUB_CONFIG,
    [string] $Binary = $env:AI_STATUS_HUB_BINARY
)

$ErrorActionPreference = "Stop"

function Get-RepoRoot {
    $scriptDir = Split-Path -Parent $PSCommandPath
    return (Resolve-Path (Join-Path $scriptDir "..")).Path
}

function Get-TomlValue {
    param(
        [string] $Path,
        [string] $Section,
        [string] $Key
    )

    $inSection = $false
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^\s*\[(.+)\]\s*$') {
            $inSection = ($Matches[1] -eq $Section)
            continue
        }

        if ($inSection -and $line -match "^\s*$Key\s*=\s*(.+?)\s*(#.*)?$") {
            return $Matches[1].Trim().Trim('"')
        }
    }

    return $null
}

function Test-HubHealth {
    param([string] $Url)

    try {
        $response = Invoke-WebRequest -UseBasicParsing -Uri "$Url/health" -TimeoutSec 2
        return ($response.StatusCode -ge 200 -and $response.StatusCode -lt 300)
    } catch {
        return $false
    }
}

$repoRoot = Get-RepoRoot
Set-Location $repoRoot

if (-not $Config) {
    $Config = Join-Path $repoRoot "config.toml"
}
if (-not (Test-Path -LiteralPath $Config)) {
    $Config = Join-Path $repoRoot "config.example.toml"
}
$Config = (Resolve-Path -LiteralPath $Config).Path

if (-not $Binary) {
    $Binary = Join-Path $repoRoot "target\release\aistatushub.exe"
}
if (-not (Test-Path -LiteralPath $Binary)) {
    $Binary = Join-Path $repoRoot "target\debug\aistatushub.exe"
}
if (-not (Test-Path -LiteralPath $Binary)) {
    throw "AIStatusHub binary was not found. Build it with: cargo build --release"
}
$Binary = (Resolve-Path -LiteralPath $Binary).Path

$serverHost = Get-TomlValue -Path $Config -Section "server" -Key "host"
$serverPort = Get-TomlValue -Path $Config -Section "server" -Key "port"
if (-not $serverHost) {
    $serverHost = "127.0.0.1"
}
if (-not $serverPort) {
    $serverPort = "17888"
}
$browserHost = $serverHost
if ($browserHost -eq "0.0.0.0" -or $browserHost -eq "::") {
    $browserHost = "127.0.0.1"
}
$hubUrl = "http://${browserHost}:${serverPort}"

$logDir = Join-Path $repoRoot "logs"
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$stdoutLog = Join-Path $logDir "aistatushub-service.out.log"
$stderrLog = Join-Path $logDir "aistatushub-service.err.log"

$startedAt = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
"[$startedAt] AIStatusHub service runner starting. binary=$Binary config=$Config" | Out-File -FilePath $stdoutLog -Append -Encoding UTF8

if (Test-HubHealth -Url $hubUrl) {
    "[$startedAt] AIStatusHub is already healthy at $hubUrl; service runner exiting." | Out-File -FilePath $stdoutLog -Append -Encoding UTF8
    exit 0
}

$env:AI_STATUS_HUB_OPEN_BROWSER = "0"
$env:RUST_LOG = $(if ($env:RUST_LOG) { $env:RUST_LOG } else { "aistatushub=info,tower_http=warn,axum=warn" })
$env:NO_COLOR = "1"

$command = "`"$Binary`" --config `"$Config`" >> `"$stdoutLog`" 2>> `"$stderrLog`""
& $env:ComSpec /d /c $command
exit $LASTEXITCODE
