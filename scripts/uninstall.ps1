#Requires -Version 5.1
<#
.SYNOPSIS
    Uninstalls the gitar engine and Python control server on Windows.
.DESCRIPTION
    Removes gitar-engine.exe, takes the install directory off the user PATH, and
    (unless -KeepConfig is given) removes the per-user configuration directory
    %APPDATA%\gitar. No administrator rights are required. The Python package is
    not touched here; remove it with: python -m pip uninstall gitar
.PARAMETER BinDir
    Directory gitar-engine.exe was installed into.
    Default: %LOCALAPPDATA%\gitar\bin.
.PARAMETER KeepConfig
    Keep %APPDATA%\gitar (settings and downloaded models).
.EXAMPLE
    ./uninstall.ps1
.EXAMPLE
    ./uninstall.ps1 -KeepConfig
#>
[CmdletBinding()]
param(
    [string]$BinDir = (Join-Path $env:LOCALAPPDATA "gitar\bin"),
    [switch]$KeepConfig
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ConfigDir = Join-Path $env:APPDATA "gitar"

function Write-Ok {
    param([string]$Message)
    Write-Host "  $Message" -ForegroundColor Green
}

function Write-Warn {
    param([string]$Message)
    Write-Host "  $Message" -ForegroundColor Yellow
}

Write-Host "gitar uninstaller for Windows" -ForegroundColor White

Write-Host "==> Removing the engine" -ForegroundColor Cyan
$exe = Join-Path $BinDir "gitar-engine.exe"
if (Test-Path $exe) {
    Remove-Item -Path $exe -Force
    Write-Ok "removed $exe"
}
if ((Test-Path $BinDir) -and -not (Get-ChildItem -Path $BinDir -Force)) {
    Remove-Item -Path $BinDir -Force
    Write-Ok "removed empty directory $BinDir"
}

Write-Host "==> Updating the user PATH" -ForegroundColor Cyan
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ([string]::IsNullOrEmpty($userPath)) {
    Write-Warn "$BinDir is not on the user PATH"
} else {
    $entries = @($userPath -split ";" | Where-Object { $_ -ne "" })
    $kept = @($entries | Where-Object { $_.TrimEnd("\") -ine $BinDir.TrimEnd("\") })
    if ($kept.Count -eq $entries.Count) {
        Write-Warn "$BinDir is not on the user PATH"
    } else {
        if ($kept.Count -eq 0) {
            [Environment]::SetEnvironmentVariable("Path", $null, "User")
        } else {
            [Environment]::SetEnvironmentVariable("Path", ($kept -join ";"), "User")
        }
        Write-Ok "removed $BinDir from the user PATH (open a new terminal to apply it)"
        try {
            Add-Type -Namespace Gitar -Name Native -MemberDefinition @"
[System.Runtime.InteropServices.DllImport("user32.dll", SetLastError = true, CharSet = System.Runtime.InteropServices.CharSet.Auto)]
public static extern System.IntPtr SendMessageTimeout(System.IntPtr hWnd, uint Msg, System.UIntPtr wParam, string lParam, uint fuFlags, uint uTimeout, out System.UIntPtr lpdwResult);
"@
            $result = [System.UIntPtr]::Zero
            [Gitar.Native]::SendMessageTimeout(
                [System.IntPtr]0xffff, 0x1A, [System.UIntPtr]::Zero, "Environment", 2, 5000,
                [ref]$result) | Out-Null
        } catch {
            Write-Warn "Could not broadcast the environment change; new terminals will still pick it up"
        }
    }
}

Write-Host "==> Removing configuration" -ForegroundColor Cyan
if ($KeepConfig) {
    Write-Warn "kept $ConfigDir"
} elseif (Test-Path $ConfigDir) {
    Remove-Item -Path $ConfigDir -Recurse -Force
    Write-Ok "removed $ConfigDir (settings, models, and logs inside it)"
} else {
    Write-Warn "$ConfigDir does not exist"
}

Write-Host ""
Write-Host "gitar uninstallation finished." -ForegroundColor Green
Write-Host "Not removed:"
Write-Host "  - the Python package (python -m pip uninstall gitar)"
Write-Host "  - models outside $ConfigDir (for example a custom GITAR_MODELS_DIR)"
