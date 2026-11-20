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
Ensure-Directory -Path $ResolvedBuildDir
Ensure-Directory -Path $ToolchainDir

if ([string]::IsNullOrWhiteSpace($ProxyUrl)) {
    $ProxyUrl = "http://localhost:10082"
    Write-Host "No -ProxyUrl given; using $ProxyUrl for pacman downloads." -ForegroundColor Yellow
}
Set-ToolchainsProxyEnvironment -ProxyUrl $ProxyUrl | Out-Null
Install-Msys2Distribution -ProjectRoot $ProjectRoot -BuildDir $ResolvedBuildDir | Out-Null
Update-Msys2Packages -BuildDir $ResolvedBuildDir

$packages = @(
    "mingw-w64-ucrt-x86_64-gcc",
    "mingw-w64-ucrt-x86_64-gdb",
    "mingw-w64-ucrt-x86_64-cmake",
    "mingw-w64-ucrt-x86_64-ninja",
    "mingw-w64-ucrt-x86_64-pkgconf",
    "mingw-w64-ucrt-x86_64-boost",
    "mingw-w64-ucrt-x86_64-wxwidgets3.2-common",
    "mingw-w64-ucrt-x86_64-wxwidgets3.2-msw",
    "mingw-w64-ucrt-x86_64-wxwidgets3.2-msw-libs",
    "mingw-w64-ucrt-x86_64-nlohmann-json"
)

$installCommand = "pacman --noconfirm --disable-download-timeout --needed -S $($packages -join ' ')"
Write-Host "Installing Windows UCRT64 libraries via pacman..." -ForegroundColor Green
Invoke-Msys2Command -BuildDir $ResolvedBuildDir -Command $installCommand -UseUcrt64

$summaryPath = Join-Path $ToolchainDir "libraries-ucrt64.txt"
$summaryPathMsys = Convert-WindowsPathToMsysPath -Path $summaryPath
Invoke-Msys2Command -BuildDir $ResolvedBuildDir -Command "pacman -Q > `"$summaryPathMsys`"" -UseUcrt64

Write-Host "Windows UCRT64 libraries are ready under $(Join-Path $ResolvedBuildDir 'msys64\ucrt64')" -ForegroundColor Green
