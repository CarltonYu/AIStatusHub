param(
    [string] $TaskName = "AIStatusHub",
    [string] $HubUrl = "http://127.0.0.1:17888"
)

$ErrorActionPreference = "Continue"

$task = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
if ($task) {
    $info = Get-ScheduledTaskInfo -TaskName $TaskName
    Write-Host "Task: $TaskName"
    Write-Host "State: $($task.State)"
    Write-Host "LastRunTime: $($info.LastRunTime)"
    Write-Host "LastTaskResult: $($info.LastTaskResult)"
    Write-Host "NextRunTime: $($info.NextRunTime)"
} else {
    Write-Host "Task not installed: $TaskName"
}

try {
    $health = Invoke-RestMethod -Uri "$HubUrl/health" -TimeoutSec 2
    Write-Host "Hub health: OK ($HubUrl)"
    $health | ConvertTo-Json -Depth 5
} catch {
    Write-Host "Hub health: FAILED ($HubUrl)"
    Write-Host $_.Exception.Message
}

$processes = Get-Process aistatushub -ErrorAction SilentlyContinue
if ($processes) {
    Write-Host "Processes:"
    $processes | Select-Object Id,Path,StartTime | Format-Table -AutoSize
} else {
    Write-Host "No aistatushub process found."
}
