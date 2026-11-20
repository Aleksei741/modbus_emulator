param (
    [string]$ProjectRoot,
    [string]$ProjectName,
    [string]$ToolchainBuildDir,
    [string]$BuildOutputDir,
    [string]$PackageOutputDir,
    [string]$ProxyUrl,
    [string]$BuildType = "Debug"
)

$ErrorActionPreference = "Stop"

. "$PSScriptRoot\..\..\toolchains\common\ToolchainHelpers.ps1"

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)
chcp 65001 | Out-Null

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    throw "ProjectRoot is required."
}

if ([string]::IsNullOrWhiteSpace($ProjectName)) {
    throw "ProjectName is required."
}

$ResolvedBuildDir = Resolve-ToolchainBuildDir -ProjectRoot $ProjectRoot -BuildDir $ToolchainBuildDir
if ([string]::IsNullOrWhiteSpace($BuildOutputDir)) {
    $BuildOutputDir = Join-Path $ProjectRoot "build\windows-x64\$BuildType"
}

Ensure-Directory -Path $ResolvedBuildDir
Ensure-Directory -Path $BuildOutputDir

Set-ToolchainsProxyEnvironment -ProxyUrl $ProxyUrl | Out-Null
Install-Msys2Distribution -ProjectRoot $ProjectRoot -BuildDir $ResolvedBuildDir | Out-Null

$buildOutputDirMsys = Convert-WindowsPathToMsysPath -Path $BuildOutputDir
$projectRootMsys = Convert-WindowsPathToMsysPath -Path $ProjectRoot

$ucrtRoot = Join-Path $ResolvedBuildDir "msys64\ucrt64"
$ucrtRootCmake = ($ucrtRoot -replace "\\", "/")

$configureCommand = @(
    "cmake",
    "-S", "$projectRootMsys/building",
    "-B", "$buildOutputDirMsys",
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=$BuildType",
    "-DCMAKE_PREFIX_PATH=/ucrt64"
) -join " "

Write-Host "Configuring Windows x64 Modbus RTU emulator with MSYS2 UCRT64..." -ForegroundColor Green
Invoke-Msys2Command -BuildDir $ResolvedBuildDir -WorkingDirectory $ProjectRoot -Command $configureCommand -UseUcrt64

Write-Host "Building Windows x64 Modbus RTU apps (${BuildType}) with MSYS2 UCRT64..." -ForegroundColor Green
Invoke-Msys2Command -BuildDir $ResolvedBuildDir -WorkingDirectory $ProjectRoot -Command "cmake --build `"$buildOutputDirMsys`" --parallel --target modbus_rtu_emulator modbus_rtu_poller" -UseUcrt64

$apps = @("modbus_rtu_emulator", "modbus_rtu_poller")
foreach ($app in $apps) {
    $runtimeDir = Join-Path $BuildOutputDir $app
    $appExe = Join-Path $runtimeDir "$app.exe"
    if (-not (Test-Path $appExe)) {
        throw "$app.exe was not built: $appExe"
    }

    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\bundle_runtime.ps1" `
        -BinaryPath $appExe `
        -SearchRoot $ucrtRoot `
        -OutputDirectory $runtimeDir
}

if ($BuildType -ieq "Release") {
    if ([string]::IsNullOrWhiteSpace($PackageOutputDir)) {
        $PackageOutputDir = Join-Path $ProjectRoot "dist\windows-x64"
    }

    New-Item -ItemType Directory -Force -Path $PackageOutputDir | Out-Null
    foreach ($app in $apps) {
        $runtimeDir = Join-Path $BuildOutputDir $app
        $packageDir = Join-Path $PackageOutputDir $app
        if (Test-Path $packageDir) {
            Remove-Item -LiteralPath $packageDir -Recurse -Force
        }
        Copy-Item -LiteralPath $runtimeDir -Destination $packageDir -Recurse -Force
        Write-Host "Windows x64 Release package (no CMake files): $packageDir" -ForegroundColor Green
    }
}

Write-Host "Windows x64 build finished. Output directory: $BuildOutputDir" -ForegroundColor Green
