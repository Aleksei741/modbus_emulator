param (
    [string]$ToolchainBuildDir,
    [string]$BuildOutputDir,
    [string]$ExecutableName = "modbus_rtu_poller"
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($ToolchainBuildDir)) {
    throw "ToolchainBuildDir is required."
}

if ([string]::IsNullOrWhiteSpace($BuildOutputDir)) {
    throw "BuildOutputDir is required."
}

$executableCandidates = @(
    (Join-Path $BuildOutputDir "$ExecutableName\$ExecutableName.exe"),
    (Join-Path $BuildOutputDir "$ExecutableName.exe")
)
$executablePath = $executableCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $executablePath) {
    throw "Modbus RTU poller not found. Looked in: $($executableCandidates -join ', ')"
}

$exeDir = Split-Path -Parent $executablePath
$env:PATH = "$exeDir;$env:PATH"

Write-Host "Starting $executablePath" -ForegroundColor Green
& $executablePath
if ($LASTEXITCODE -ne 0 -and $null -ne $LASTEXITCODE) {
    throw "Poller exited with code $LASTEXITCODE"
}
