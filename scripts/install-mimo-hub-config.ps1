param(
    [string] $ConfigDir = $(Join-Path $env:USERPROFILE ".config\mimocode"),
    [string] $ProviderId = "aistatushub-mimo",
    [string] $ApiBase = "http://127.0.0.1:17888/mimo/v1"
)

$ErrorActionPreference = "Stop"

$provider = [ordered]@{
    name = "AIStatusHub MiMo"
    npm = "@ai-sdk/openai-compatible"
    api = $ApiBase
    options = [ordered]@{
        apiKey = "anonymous"
    }
    models = [ordered]@{
        "mimo-auto" = [ordered]@{
            name = "MiMo Auto via AIStatusHub"
            attachment = $true
            reasoning = $true
            tool_call = $true
            temperature = $true
            modalities = [ordered]@{
                input = @("text", "image")
                output = @("text")
            }
            limit = [ordered]@{
                context = 1000000
                output = 128000
            }
            cost = [ordered]@{
                input = 0
                output = 0
            }
        }
    }
}

New-Item -ItemType Directory -Path $ConfigDir -Force | Out-Null
$configPath = Join-Path $ConfigDir "mimocode.json"

if (Test-Path -LiteralPath $configPath) {
    $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
    Copy-Item -LiteralPath $configPath -Destination "$configPath.bak-$timestamp" -Force
    $config = Get-Content -Raw -LiteralPath $configPath | ConvertFrom-Json
} else {
    $config = [pscustomobject]@{}
}

if (-not ($config.PSObject.Properties.Name -contains "provider")) {
    $config | Add-Member -MemberType NoteProperty -Name "provider" -Value ([pscustomobject]@{})
}

if ($config.provider.PSObject.Properties.Name -contains $ProviderId) {
    $config.provider.PSObject.Properties.Remove($ProviderId)
}

$config.provider | Add-Member -MemberType NoteProperty -Name $ProviderId -Value $provider
$config | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $configPath -Encoding UTF8

Write-Host "Installed Mimo provider '$ProviderId' into $configPath"
