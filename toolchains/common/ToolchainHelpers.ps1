function Resolve-ToolchainBuildDir {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot,
        [string]$BuildDir
    )

    if ([string]::IsNullOrWhiteSpace($BuildDir)) {
        return (Join-Path $ProjectRoot "toolchain_Cxx")
    }

    return $BuildDir
}

function Ensure-Directory {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (!(Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path $Path -Force -ErrorAction Stop | Out-Null
    }
}

function Get-7ZipExecutable {
    $candidates = @(
        "7z",
        "7z.exe",
        (Join-Path ${env:ProgramFiles} "7-Zip\7z.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "7-Zip\7z.exe")
    ) | Where-Object { $_ }

    foreach ($candidate in $candidates) {
        $command = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($command) {
            return $command.Source
        }

        if (Test-Path $candidate) {
            return $candidate
        }
    }

    throw "7-Zip executable was not found. Install 7-Zip or add `7z.exe` to PATH."
}

function Expand-7ZipArchive {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ArchivePath,
        [Parameter(Mandatory = $true)]
        [string]$DestinationDir
    )

    if (!(Test-Path $ArchivePath)) {
        throw "Archive not found: $ArchivePath"
    }

    Ensure-Directory -Path $DestinationDir
    $sevenZip = Get-7ZipExecutable
    & $sevenZip x $ArchivePath "-o$DestinationDir" -y | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to extract archive: $ArchivePath"
    }
}

function Set-ToolchainsProxyEnvironment {
    param(
        [string]$ProxyUrl
    )

    if ([string]::IsNullOrWhiteSpace($ProxyUrl)) {
        return $false
    }

    $resolvedProxyUrl = $ProxyUrl

    $proxyVariables = @(
        "http_proxy",
        "https_proxy",
        "HTTP_PROXY",
        "HTTPS_PROXY",
        "all_proxy",
        "ALL_PROXY"
    )

    foreach ($name in $proxyVariables) {
        [System.Environment]::SetEnvironmentVariable($name, $resolvedProxyUrl, "Process")
    }

    if (-not [string]::IsNullOrWhiteSpace($env:NO_PROXY)) {
        [System.Environment]::SetEnvironmentVariable("NO_PROXY", $env:NO_PROXY, "Process")
        [System.Environment]::SetEnvironmentVariable("no_proxy", $env:NO_PROXY, "Process")
    }
    elseif (-not [string]::IsNullOrWhiteSpace($env:no_proxy)) {
        [System.Environment]::SetEnvironmentVariable("NO_PROXY", $env:no_proxy, "Process")
        [System.Environment]::SetEnvironmentVariable("no_proxy", $env:no_proxy, "Process")
    }

    Write-Host "Using proxy for external downloads: $resolvedProxyUrl" -ForegroundColor Green
    return $true
}

function Convert-WindowsPathToMsysPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $resolvedPath = [System.IO.Path]::GetFullPath($Path)
    $normalizedPath = $resolvedPath -replace "\\", "/"
    if ($normalizedPath -match "^([A-Za-z]):/(.*)$") {
        return "/$($matches[1].ToLowerInvariant())/$($matches[2])"
    }

    return $normalizedPath
}

function Get-Msys2InstallRoot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BuildDir
    )

    return (Join-Path $BuildDir "msys64")
}

function Get-Msys2InstallerCacheDir {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BuildDir
    )

    return (Join-Path $BuildDir "msys2-installer-cache")
}

function Get-Msys2ShellExecutable {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BuildDir
    )

    $bashExe = Join-Path (Get-Msys2InstallRoot -BuildDir $BuildDir) "usr\bin\bash.exe"
    if (!(Test-Path $bashExe)) {
        throw "MSYS2 bash.exe was not found at $bashExe. Run the MSYS2 setup step first."
    }

    return $bashExe
}

function Ensure-Msys2InstallerExecutable {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot,
        [Parameter(Mandatory = $true)]
        [string]$BuildDir
    )

    $archivePath = Join-Path $ProjectRoot "toolchains\third_party\msys2-x86_64-latest.7z"
    if (!(Test-Path $archivePath)) {
        throw "MSYS2 installer archive not found: $archivePath"
    }

    $installerDir = Join-Path $BuildDir "msys2-installer"
    Ensure-Directory -Path $installerDir

    $installerExe = Get-ChildItem -Path $installerDir -Filter "msys2-*.exe" -File -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $installerExe) {
        Write-Host "Extracting MSYS2 installer into $installerDir" -ForegroundColor Green
        Expand-7ZipArchive -ArchivePath $archivePath -DestinationDir $installerDir
        $installerExe = Get-ChildItem -Path $installerDir -Filter "msys2-*.exe" -File -Recurse -ErrorAction Stop | Select-Object -First 1
    }

    return $installerExe.FullName
}

function Install-Msys2Distribution {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot,
        [Parameter(Mandatory = $true)]
        [string]$BuildDir
    )

    $installRoot = Get-Msys2InstallRoot -BuildDir $BuildDir
    $bashExe = Join-Path $installRoot "usr\bin\bash.exe"
    if (Test-Path $bashExe) {
        return $installRoot
    }

    $installerExe = Ensure-Msys2InstallerExecutable -ProjectRoot $ProjectRoot -BuildDir $BuildDir
    $installerRoot = ($installRoot -replace "\\", "/")
    $cacheDir = Get-Msys2InstallerCacheDir -BuildDir $BuildDir
    Ensure-Directory -Path $cacheDir
    Write-Host "Installing MSYS2 into $installRoot" -ForegroundColor Green
    & $installerExe --confirm-command --accept-messages --accept-licenses --cache-path $cacheDir --root $installerRoot in
    if ($LASTEXITCODE -ne 0) {
        throw "MSYS2 installer failed with exit code $LASTEXITCODE"
    }

    if (!(Test-Path $bashExe)) {
        throw "MSYS2 installation completed, but bash.exe was not found at $bashExe"
    }

    return $installRoot
}

function Invoke-Msys2Command {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BuildDir,
        [Parameter(Mandatory = $true)]
        [string]$Command,
        [string]$WorkingDirectory,
        [switch]$UseUcrt64
    )

    $bashExe = Get-Msys2ShellExecutable -BuildDir $BuildDir
    $previousMsystem = $env:MSYSTEM
    $previousChere = $env:CHERE_INVOKING
    $previousPathType = $env:MSYS2_PATH_TYPE

    try {
        $env:CHERE_INVOKING = "yes"
        $env:MSYS2_PATH_TYPE = "inherit"
        if ($UseUcrt64) {
            $env:MSYSTEM = "UCRT64"
        }

        $bashCommand = $Command
        if (-not [string]::IsNullOrWhiteSpace($WorkingDirectory)) {
            $msysWorkingDirectory = Convert-WindowsPathToMsysPath -Path $WorkingDirectory
            $bashCommand = "cd `"$msysWorkingDirectory`" && $Command"
        }

        & $bashExe --login -lc $bashCommand
        if ($LASTEXITCODE -ne 0) {
            throw "MSYS2 command failed with exit code ${LASTEXITCODE}: $Command"
        }
    }
    finally {
        if ($null -eq $previousMsystem) {
            Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
        }
        else {
            $env:MSYSTEM = $previousMsystem
        }

        if ($null -eq $previousChere) {
            Remove-Item Env:CHERE_INVOKING -ErrorAction SilentlyContinue
        }
        else {
            $env:CHERE_INVOKING = $previousChere
        }

        if ($null -eq $previousPathType) {
            Remove-Item Env:MSYS2_PATH_TYPE -ErrorAction SilentlyContinue
        }
        else {
            $env:MSYS2_PATH_TYPE = $previousPathType
        }
    }
}

function Update-Msys2Packages {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BuildDir
    )

    Write-Host "Updating MSYS2 package databases and base system..." -ForegroundColor Green

    $updateCommand = "pacman --noconfirm --disable-download-timeout -Syuu"
    $maxAttempts = 3
    for ($attempt = 1; $attempt -le $maxAttempts; $attempt++) {
        try {
            # pacman often wants a second pass after it upgrades itself.
            Invoke-Msys2Command -BuildDir $BuildDir -Command $updateCommand -UseUcrt64
            Invoke-Msys2Command -BuildDir $BuildDir -Command $updateCommand -UseUcrt64
            return
        }
        catch {
            if ($attempt -eq $maxAttempts) {
                throw
            }

            Write-Warning "MSYS2 update failed (attempt $attempt of $maxAttempts): $($_.Exception.Message)"
            Start-Sleep -Seconds (5 * $attempt)
        }
    }
}

function Remove-WindowsMsvcArtifacts {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BuildDir
    )

    $artifactPaths = @(
        (Join-Path $BuildDir "toolchain-windows-msvc")
    )

    foreach ($artifactPath in $artifactPaths) {
        if (Test-Path -LiteralPath $artifactPath) {
            Write-Host "Removing legacy Windows MSVC artifact $artifactPath" -ForegroundColor Yellow
            Remove-Item -LiteralPath $artifactPath -Recurse -Force
        }
    }
}
