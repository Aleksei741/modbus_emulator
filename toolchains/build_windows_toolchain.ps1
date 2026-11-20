param (
    [string]$ProjectRoot,
    [string]$ProjectName,
    [string]$BuildDir,
    [string]$ProxyUrl
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    throw "ProjectRoot is required."
}

if ([string]::IsNullOrWhiteSpace($ProjectName)) {
    throw "ProjectName is required."
}

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $ProjectRoot "toolchain_build"
}

function Invoke-ToolchainStep {
    param(
        [string]$Name,
        [string]$ScriptPath,
        [string[]]$Arguments = @()
    )

    Write-Host "=== $Name ===" -ForegroundColor Cyan
    & powershell.exe -ExecutionPolicy Bypass -File $ScriptPath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE"
    }
}

$SetupScript = Join-Path $PSScriptRoot "toolchains\windows-ucrt64\setup_windows_ucrt64.ps1"
$InstallLibrariesScript = Join-Path $PSScriptRoot "library\windows-ucrt64\install_libraries_windows_ucrt64.ps1"

$CommonArgs = @(
    "-ProjectRoot", $ProjectRoot,
    "-ProjectName", $ProjectName,
    "-BuildDir", $BuildDir
)

if (-not [string]::IsNullOrWhiteSpace($ProxyUrl)) {
    $CommonArgs += @("-ProxyUrl", $ProxyUrl)
}

Invoke-ToolchainStep `
    -Name "Windows UCRT64 Setup" `
    -ScriptPath $SetupScript `
    -Arguments $CommonArgs

Invoke-ToolchainStep `
    -Name "Windows UCRT64 Libraries" `
    -ScriptPath $InstallLibrariesScript `
    -Arguments $CommonArgs

Write-Host "Windows UCRT64 toolchain is ready." -ForegroundColor Green
