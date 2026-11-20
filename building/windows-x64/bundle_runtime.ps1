param (
    [string]$BinaryPath,
    [string]$SearchRoot,
    [string]$OutputDirectory
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($BinaryPath) -or -not (Test-Path $BinaryPath)) {
    throw "Binary not found: $BinaryPath"
}

if ([string]::IsNullOrWhiteSpace($SearchRoot)) {
    throw "SearchRoot is required."
}

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Split-Path -Parent $BinaryPath
}

function Ensure-Directory {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
}

function Resolve-Objdump {
    param([string]$Root)

    foreach ($candidate in @((Join-Path $Root "bin\objdump.exe"), (Join-Path $Root "bin\objdump"))) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    $fromPath = Get-Command objdump -ErrorAction SilentlyContinue
    if ($fromPath) {
        return $fromPath.Source
    }

    throw "objdump not found under $Root. Install the UCRT64 toolchain first."
}

function Get-ImportedDlls {
    param(
        [string]$ObjdumpPath,
        [string]$PePath
    )

    $output = & $ObjdumpPath -p $PePath 2>$null
    $names = New-Object System.Collections.Generic.List[string]
    foreach ($line in $output) {
        if ($line -match 'DLL Name:\s+(\S+)') {
            $names.Add($matches[1])
        }
    }
    return $names
}

function Find-ToolchainDll {
    param(
        [string]$Name,
        [string[]]$SearchDirectories
    )

    foreach ($dir in $SearchDirectories) {
        $candidate = Join-Path $dir $Name
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    return $null
}

Ensure-Directory -Path $OutputDirectory

$objdump = Resolve-Objdump -Root $SearchRoot
$toolchainDirectories = @(
    (Join-Path $SearchRoot "bin"),
    (Join-Path $SearchRoot "lib")
) | Where-Object { Test-Path $_ }

$queue = New-Object System.Collections.Generic.Queue[string]
$visitedFiles = New-Object System.Collections.Generic.HashSet[string] ([System.StringComparer]::OrdinalIgnoreCase)
$resolvedNames = New-Object System.Collections.Generic.HashSet[string] ([System.StringComparer]::OrdinalIgnoreCase)
$copied = New-Object System.Collections.Generic.List[string]

function Enqueue-Pe {
    param([string]$Path)
    if (Test-Path $Path) {
        $queue.Enqueue((Resolve-Path $Path).Path)
    }
}

Enqueue-Pe -Path $BinaryPath

Get-ChildItem -Path $OutputDirectory -Recurse -Filter *.dll -File -ErrorAction SilentlyContinue |
    ForEach-Object { Enqueue-Pe -Path $_.FullName }

while ($queue.Count -gt 0) {
    $current = $queue.Dequeue()
    if (-not $visitedFiles.Add($current)) {
        continue
    }

    foreach ($dependency in (Get-ImportedDlls -ObjdumpPath $objdump -PePath $current)) {
        if (-not $resolvedNames.Add($dependency)) {
            continue
        }

        $already = Join-Path $OutputDirectory $dependency
        if (Test-Path $already) {
            Enqueue-Pe -Path $already
            continue
        }

        $fromToolchain = Find-ToolchainDll -Name $dependency -SearchDirectories $toolchainDirectories
        if ($null -eq $fromToolchain) {
            continue
        }

        $target = Join-Path $OutputDirectory ([System.IO.Path]::GetFileName($fromToolchain))
        Copy-Item -LiteralPath $fromToolchain -Destination $target -Force
        $copied.Add([System.IO.Path]::GetFileName($fromToolchain))
        Enqueue-Pe -Path $target
    }
}

Write-Host ("Copied {0} toolchain DLLs required by {1}" -f $copied.Count, (Split-Path $BinaryPath -Leaf)) -ForegroundColor Green
