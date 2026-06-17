param(
    [string] $Config = $env:AI_STATUS_HUB_CONFIG,
    [string] $DuoHost = $env:AI_STATUS_HUB_DUO_HOST,
    [int] $DuoPort = $(if ($env:AI_STATUS_HUB_DUO_PORT) { [int]$env:AI_STATUS_HUB_DUO_PORT } else { 25250 }),
    [switch] $DuoProbe,
    [switch] $SkipDuoProbe,
    [switch] $NoBrowser,
    [switch] $Release
)

$ErrorActionPreference = "Stop"

function Get-RepoRoot {
    $scriptDir = Split-Path -Parent $PSCommandPath
    if (Test-Path (Join-Path $scriptDir "Cargo.toml")) {
        return (Resolve-Path $scriptDir).Path
    }

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
        $response = Invoke-WebRequest -UseBasicParsing -Uri "$Url/health" -TimeoutSec 1
        return ($response.StatusCode -ge 200 -and $response.StatusCode -lt 300)
    } catch {
        return $false
    }
}

function Send-DuoProbe {
    param(
        [string] $HostName,
        [int] $Port
    )

    $client = $null
    try {
        $client = [System.Net.Sockets.UdpClient]::new()
        $client.Client.ReceiveTimeout = 800
        $client.Connect($HostName, $Port)
        $bytes = [System.Text.Encoding]::UTF8.GetBytes("normal`n")
        [void] $client.Send($bytes, $bytes.Length)

        try {
            $remote = [System.Net.IPEndPoint]::new([System.Net.IPAddress]::Any, 0)
            [void] $client.Receive([ref] $remote)
        } catch {
            # Some UDP stacks/firewalls drop the reply; the send itself is enough for startup.
        }

        return $true
    } catch {
        return $false
    } finally {
        if ($client) {
            $client.Dispose()
        }
    }
}

function Quote-ProcessArg {
    param([string] $Value)

    if ($Value -match '[\s"]') {
        return '"' + ($Value -replace '"', '\"') + '"'
    }

    return $Value
}

function Start-HubProcess {
    param(
        [string] $RepoRoot,
        [string] $ConfigPath,
        [bool] $UseRelease
    )

    $cargo = Get-Command cargo -ErrorAction SilentlyContinue
    if ($cargo) {
        $args = @("run")
        if ($UseRelease) {
            $args += "--release"
        }
        $args += @("--", "--config", (Quote-ProcessArg $ConfigPath))
        return Start-Process -FilePath $cargo.Source -ArgumentList $args -WorkingDirectory $RepoRoot -NoNewWindow -PassThru
    }

    $binary = Join-Path $RepoRoot "target\debug\aistatushub.exe"
    if (-not (Test-Path -LiteralPath $binary)) {
        throw "cargo is not installed and $binary is missing."
    }

    return Start-Process -FilePath $binary -ArgumentList @("--config", (Quote-ProcessArg $ConfigPath)) -WorkingDirectory $RepoRoot -NoNewWindow -PassThru
}

$repoRoot = Get-RepoRoot
Set-Location $repoRoot

if (-not $Config) {
    $Config = Join-Path $repoRoot "config.toml"
}
if (-not (Test-Path -LiteralPath $Config)) {
    $exampleConfig = Join-Path $repoRoot "config.example.toml"
    if (Test-Path -LiteralPath $exampleConfig) {
        $Config = $exampleConfig
    } else {
        throw "config.toml was not found."
    }
}
$Config = (Resolve-Path -LiteralPath $Config).Path

if (-not $DuoHost) {
    $DuoHost = "192.168.42.1"
}

$serverHost = $env:AI_STATUS_HUB_HOST
if (-not $serverHost) {
    $serverHost = Get-TomlValue -Path $Config -Section "server" -Key "host"
}
if (-not $serverHost) {
    $serverHost = "127.0.0.1"
}

$serverPort = $env:AI_STATUS_HUB_PORT
if (-not $serverPort) {
    $serverPort = Get-TomlValue -Path $Config -Section "server" -Key "port"
}
if (-not $serverPort) {
    $serverPort = "17888"
}

$browserHost = $serverHost
if ($browserHost -eq "0.0.0.0" -or $browserHost -eq "::") {
    $browserHost = "127.0.0.1"
}
$hubUrl = "http://${browserHost}:${serverPort}"

if (Test-HubHealth -Url $hubUrl) {
    Write-Host "AIStatusHub is already running: $hubUrl"
    if (-not $NoBrowser -and $env:AI_STATUS_HUB_OPEN_BROWSER -ne "0") {
        Start-Process $hubUrl | Out-Null
    }
    exit 0
}

Write-Host "AIStatusHub root: $repoRoot"
Write-Host "Config: $Config"
Write-Host "Web UI: $hubUrl"

if (($DuoProbe -or $env:AI_STATUS_HUB_DUO_PROBE -eq "1") -and -not $SkipDuoProbe -and $env:AI_STATUS_HUB_SKIP_DUO_PROBE -ne "1") {
    if (Send-DuoProbe -HostName $DuoHost -Port $DuoPort) {
        Write-Host "Duo face probe sent to udp://${DuoHost}:${DuoPort}"
    } else {
        Write-Host "Duo face probe failed; continuing."
    }
}

$process = Start-HubProcess -RepoRoot $repoRoot -ConfigPath $Config -UseRelease ($Release -or $env:AI_STATUS_HUB_RELEASE -eq "1")

$ready = $false
for ($i = 0; $i -lt 90; $i++) {
    if ($process.HasExited) {
        break
    }

    if (Test-HubHealth -Url $hubUrl) {
        $ready = $true
        Write-Host "AIStatusHub is ready: $hubUrl"
        if (-not $NoBrowser -and $env:AI_STATUS_HUB_OPEN_BROWSER -ne "0") {
            Start-Process $hubUrl | Out-Null
        }
        break
    }

    Start-Sleep -Seconds 1
}

if (-not $ready -and -not $process.HasExited) {
    Write-Host "AIStatusHub started, but /health did not respond yet: $hubUrl"
}

Wait-Process -Id $process.Id
$process.Refresh()
exit $process.ExitCode
