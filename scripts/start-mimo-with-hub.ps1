[CmdletBinding(PositionalBinding = $false)]
param(
    [string] $Project = "",
    [string] $MimoExe = $(if ($env:MIMO_EXE) { $env:MIMO_EXE } else { "C:\ai\mimo.exe" }),
    [string] $Model = $(if ($env:MIMO_MODEL) { $env:MIMO_MODEL } else { "aistatushub-mimo/mimo-auto" }),
    [switch] $NoHub,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $MimoArgs
)

$ErrorActionPreference = "Stop"

function Get-ExplicitModel {
    param([string[]] $InputArgs)

    for ($i = 0; $i -lt $InputArgs.Count; $i++) {
        $arg = $InputArgs[$i]
        if ($arg -eq "-m" -or $arg -eq "--model") {
            if ($i + 1 -lt $InputArgs.Count) {
                return $InputArgs[$i + 1]
            }
            return ""
        }

        if ($arg -like "--model=*") {
            return $arg.Substring("--model=".Length)
        }
    }

    return $null
}

function Test-HelpOrVersion {
    param([string[]] $InputArgs)

    foreach ($arg in $InputArgs) {
        if ($arg -in @("-h", "--help", "-v", "--version")) {
            return $true
        }
    }

    return $false
}

function Get-FirstNonOption {
    param([string[]] $InputArgs)

    $optionsWithValue = @(
        "-m", "--model",
        "-s", "--session",
        "--port",
        "--hostname",
        "--mdns-domain",
        "--prompt",
        "--agent",
        "--log-level"
    )

    for ($i = 0; $i -lt $InputArgs.Count; $i++) {
        $arg = $InputArgs[$i]
        if ($arg.StartsWith("-")) {
            if ($arg -in $optionsWithValue -and $i + 1 -lt $InputArgs.Count) {
                $i++
            }
            continue
        }

        return $arg
    }

    return $null
}

function Test-ShouldUseHubModel {
    param([string[]] $InputArgs)

    if (Test-HelpOrVersion -InputArgs $InputArgs) {
        return $false
    }

    $explicitModel = Get-ExplicitModel -InputArgs $InputArgs
    if ($null -ne $explicitModel) {
        return $explicitModel -like "aistatushub-mimo/*"
    }

    $firstCommand = Get-FirstNonOption -InputArgs $InputArgs
    if (-not $firstCommand) {
        return $true
    }

    $utilityCommands = @(
        "completion",
        "acp",
        "mcp",
        "attach",
        "debug",
        "providers",
        "auth",
        "agent",
        "upgrade",
        "uninstall",
        "serve",
        "models",
        "stats",
        "export",
        "import",
        "github",
        "pr",
        "session",
        "plugin",
        "plug",
        "db"
    )

    return -not ($utilityCommands -contains $firstCommand.ToLowerInvariant())
}

function Test-HubHealth {
    try {
        $response = Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:17888/health" -TimeoutSec 1
        return ($response.StatusCode -ge 200 -and $response.StatusCode -lt 300)
    } catch {
        return $false
    }
}

function Wait-HubReady {
    for ($i = 0; $i -lt 90; $i++) {
        if (Test-HubHealth) {
            return $true
        }
        Start-Sleep -Seconds 1
    }
    return $false
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
if (-not $Project) {
    $Project = $repoRoot.Path
}
$projectPath = (Resolve-Path -LiteralPath $Project).Path

if (-not (Test-Path -LiteralPath $MimoExe)) {
    throw "Mimo executable was not found: $MimoExe"
}

$useHubModel = Test-ShouldUseHubModel -InputArgs $MimoArgs
$explicitModel = Get-ExplicitModel -InputArgs $MimoArgs

if ($env:AI_STATUS_HUB_MIMO_WRAPPER_DEBUG -eq "1") {
    Write-Host "Wrapper MimoArgs: $($MimoArgs | ConvertTo-Json -Compress)"
    Write-Host "Wrapper useHubModel: $useHubModel"
}

$installConfig = Join-Path $repoRoot "scripts\install-mimo-hub-config.ps1"
if ($useHubModel -and $env:AI_STATUS_HUB_SKIP_MIMO_CONFIG -ne "1") {
    & $installConfig | Out-Host
}

if ($useHubModel -and -not $NoHub -and -not (Test-HubHealth)) {
    $hubScript = Join-Path $repoRoot "scripts\start-ai-hub.ps1"
    Start-Process `
        -FilePath "powershell.exe" `
        -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $hubScript, "-NoBrowser", "-SkipDuoProbe") `
        -WorkingDirectory $repoRoot `
        -WindowStyle Hidden | Out-Null

    if (-not (Wait-HubReady)) {
        throw "AIStatusHub did not become ready at http://127.0.0.1:17888"
    }
}

$env:AI_STATUS_HUB_ACTIVE = "1"
$env:AI_STATUS_HUB_URL = "http://127.0.0.1:17888"
$env:AI_STATUS_HUB_MIMO_BASE_URL = "http://127.0.0.1:17888/mimo/v1"

if ($useHubModel) {
    Write-Host "AIStatusHub: http://127.0.0.1:17888"
    Write-Host "Mimo model: $(if ($explicitModel) { $explicitModel } else { $Model })"
    Write-Host "Project: $projectPath"
}

Push-Location -LiteralPath $projectPath
try {
    if ($MimoArgs.Count -gt 0) {
        $invokeArgs = @($MimoArgs)
        if ($useHubModel -and -not $explicitModel) {
            $invokeArgs += @("-m", $Model)
        }
        & $MimoExe @invokeArgs
        $mimoExitCode = $LASTEXITCODE
    } else {
        & $MimoExe $projectPath -m $Model
        $mimoExitCode = $LASTEXITCODE
    }

    exit $mimoExitCode
} finally {
    Pop-Location
}
