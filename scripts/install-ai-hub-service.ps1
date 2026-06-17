param(
    [string] $TaskName = "AIStatusHub",
    [string] $Config = $env:AI_STATUS_HUB_CONFIG,
    [switch] $StartNow
)

$ErrorActionPreference = "Stop"

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$runner = Join-Path $repoRoot "scripts\run-ai-hub-service.ps1"
$binary = Join-Path $repoRoot "target\release\aistatushub.exe"
if (-not (Test-Path -LiteralPath $binary)) {
    $binary = Join-Path $repoRoot "target\debug\aistatushub.exe"
}
if (-not (Test-Path -LiteralPath $binary)) {
    throw "AIStatusHub binary was not found. Build it first with: cargo build --release"
}

if (-not $Config) {
    $Config = Join-Path $repoRoot "config.toml"
}
if (-not (Test-Path -LiteralPath $Config)) {
    $Config = Join-Path $repoRoot "config.example.toml"
}
$Config = (Resolve-Path -LiteralPath $Config).Path
$binary = (Resolve-Path -LiteralPath $binary).Path

$logDir = Join-Path $repoRoot "logs"
New-Item -ItemType Directory -Path $logDir -Force | Out-Null

$userId = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
$encodedRunner = $runner.Replace('"', '\"')
$encodedConfig = $Config.Replace('"', '\"')
$encodedBinary = $binary.Replace('"', '\"')
$arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$encodedRunner`" -Config `"$encodedConfig`" -Binary `"$encodedBinary`""
$action = New-ScheduledTaskAction -Execute "powershell.exe" -Argument $arguments -WorkingDirectory $repoRoot

if (Test-IsAdministrator) {
    $trigger = New-ScheduledTaskTrigger -AtStartup
    $description = "Starts AIStatusHub at Windows startup as a background task."
} else {
    $trigger = New-ScheduledTaskTrigger -AtLogOn -User $userId
    $description = "Starts AIStatusHub when $userId logs on. Run installer as Administrator for true pre-login startup."
}

$settings = New-ScheduledTaskSettingsSet `
    -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries `
    -ExecutionTimeLimit (New-TimeSpan -Seconds 0) `
    -MultipleInstances IgnoreNew `
    -RestartCount 3 `
    -RestartInterval (New-TimeSpan -Minutes 1)
$principal = New-ScheduledTaskPrincipal -UserId $userId -LogonType Interactive -RunLevel Limited

Register-ScheduledTask `
    -TaskName $TaskName `
    -Description $description `
    -Action $action `
    -Trigger $trigger `
    -Settings $settings `
    -Principal $principal `
    -Force | Out-Null

Write-Host "Installed scheduled task: $TaskName"
Write-Host "User: $userId"
Write-Host "Trigger: $(if (Test-IsAdministrator) { 'At startup' } else { 'At logon' })"
Write-Host "Binary: $binary"
Write-Host "Config: $Config"
Write-Host "Logs: $logDir\aistatushub-service.out.log"

if ($StartNow) {
    Start-ScheduledTask -TaskName $TaskName
    Write-Host "Started scheduled task: $TaskName"
}
