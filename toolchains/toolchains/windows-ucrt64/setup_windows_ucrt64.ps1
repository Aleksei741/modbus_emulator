param (
    [string]$ProjectRoot,
    [string]$ProjectName,
    [string]$BuildDir,
    [string]$ProxyUrl
)

$ErrorActionPreference = "Stop"

. "$PSScriptRoot\..\..\common\ToolchainHelpers.ps1"

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)
chcp 65001 | Out-Null

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    throw "ProjectRoot is required."
}

$ResolvedBuildDir = Resolve-ToolchainBuildDir -ProjectRoot $ProjectRoot -BuildDir $BuildDir
$ToolchainDir = Join-Path $ResolvedBuildDir "toolchain-windows-ucrt64"
$Msys2Root = Get-Msys2InstallRoot -BuildDir $ResolvedBuildDir
Ensure-Directory -Path $ResolvedBuildDir
Ensure-Directory -Path $ToolchainDir

if ([string]::IsNullOrWhiteSpace($ProxyUrl)) {
    $ProxyUrl = "http://localhost:10082"
    Write-Host "No -ProxyUrl given; using $ProxyUrl for pacman downloads." -ForegroundColor Yellow
}
Set-ToolchainsProxyEnvironment -ProxyUrl $ProxyUrl | Out-Null
Remove-WindowsMsvcArtifacts -BuildDir $ResolvedBuildDir
Install-Msys2Distribution -ProjectRoot $ProjectRoot -BuildDir $ResolvedBuildDir | Out-Null
Update-Msys2Packages -BuildDir $ResolvedBuildDir

$metadataPath = Join-Path $ToolchainDir "toolchain-info.txt"
$metadata = @(
    "toolchain=windows-ucrt64"
    "project=$ProjectName"
    "root=$Msys2Root"
    "shell=$(Get-Msys2ShellExecutable -BuildDir $ResolvedBuildDir)"
    "environment=UCRT64"
)
Set-Content -Path $metadataPath -Value $metadata -Encoding UTF8

Write-Host "Windows UCRT64 toolchain is ready." -ForegroundColor Green
Write-Host "Metadata: $metadataPath" -ForegroundColor Green
