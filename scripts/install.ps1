#Requires -Version 5.1
<#
.SYNOPSIS
    Installs the gitar engine (and the Python control server) on Windows.
.DESCRIPTION
    Downloads gitar-engine.exe and, unless -NoPython is given, the gitar Python
    wheel from a GitHub release. No administrator rights are required: the engine
    goes into a per-user directory and the wheel is installed into the user
    site-packages. The user PATH is updated unless -NoPath is given.
.PARAMETER Version
    Release tag to install, for example "v2.0.0" or "2.0.0".
    Default: latest.
.PARAMETER BinDir
    Directory to install gitar-engine.exe into.
    Default: %LOCALAPPDATA%\gitar\bin.
.PARAMETER NoPython
    Do not install the Python control server.
.PARAMETER NoPath
    Do not add BinDir to the user PATH.
.EXAMPLE
    irm https://raw.githubusercontent.com/dethrandir/gitar/main/scripts/install.ps1 | iex
.EXAMPLE
    ./install.ps1 -Version v2.0.0 -NoPython
#>
[CmdletBinding()]
param(
    [string]$Version = "latest",
    [string]$BinDir = (Join-Path $env:LOCALAPPDATA "gitar\bin"),
    [switch]$NoPython,
    [switch]$NoPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Repo = "dethrandir/gitar"
$ApiBase = "https://api.github.com/repos/$Repo"
$Headers = @{ "User-Agent" = "gitar-installer" }

try {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
} catch {
    # Older/newer runtimes may not expose ServicePointManager; ignore.
}

function Write-Step {
    param([string]$Message)
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Write-Ok {
    param([string]$Message)
    Write-Host "  $Message" -ForegroundColor Green
}

function Write-Warn {
    param([string]$Message)
    Write-Host "  $Message" -ForegroundColor Yellow
}

function Get-Release {
    param([string]$Tag)
    if ($Tag -eq "latest") {
        return Invoke-RestMethod -Uri "$ApiBase/releases/latest" -Headers $Headers
    }
    $normalized = $Tag
    if (-not $normalized.StartsWith("v")) {
        $normalized = "v$normalized"
    }
    return Invoke-RestMethod -Uri "$ApiBase/releases/tags/$normalized" -Headers $Headers
}

Write-Host "gitar installer for Windows" -ForegroundColor White

Write-Step "Resolving release"
$release = Get-Release -Tag $Version
$tag = $release.tag_name
Write-Ok "Release: $tag"

$engineName = "gitar-engine-$tag-windows-x64.zip"
$engineAsset = $release.assets | Where-Object { $_.name -eq $engineName } | Select-Object -First 1
if (-not $engineAsset) {
    throw "Release $tag does not contain $engineName."
}

Write-Step "Downloading $engineName"
$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("gitar-" + [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
    $zipPath = Join-Path $tmp $engineName
    try {
        Invoke-WebRequest -Uri $engineAsset.browser_download_url -OutFile $zipPath -UseBasicParsing
    } catch {
        throw "Failed to download $engineName from $($engineAsset.browser_download_url): $_"
    }

    Write-Step "Installing gitar-engine.exe to $BinDir"
    Expand-Archive -Path $zipPath -DestinationPath $tmp -Force
    $exe = Join-Path $tmp "gitar-engine.exe"
    if (-not (Test-Path $exe)) {
        throw "The downloaded archive did not contain gitar-engine.exe."
    }
    if (-not (Test-Path $BinDir)) {
        New-Item -ItemType Directory -Path $BinDir -Force | Out-Null
    }
    Copy-Item -Path $exe -Destination (Join-Path $BinDir "gitar-engine.exe") -Force
    Write-Ok "gitar-engine.exe installed"
} finally {
    Remove-Item -Path $tmp -Recurse -Force -ErrorAction SilentlyContinue
}

if (-not $NoPath) {
    Write-Step "Updating the user PATH"
    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    if ([string]::IsNullOrEmpty($userPath)) {
        $userPath = ""
    }
    $onPath = $false
    foreach ($entry in ($userPath -split ";" | Where-Object { $_ -ne "" })) {
        if ($entry.TrimEnd("\") -ieq $BinDir.TrimEnd("\")) {
            $onPath = $true
            break
        }
    }
    if ($onPath) {
        Write-Ok "$BinDir is already on the user PATH"
    } else {
        if ($userPath -eq "") {
            $newPath = $BinDir
        } else {
            $newPath = "$BinDir;$userPath"
        }
        [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
        Write-Ok "Prepended $BinDir to the user PATH (open a new terminal to use it)"
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
} else {
    Write-Warn "Skipped the PATH update (-NoPath)"
}

if (-not $NoPython) {
    Write-Step "Installing the Python control server"
    $python = Get-Command python -ErrorAction SilentlyContinue
    if (-not $python) {
        Write-Warn "Python was not found on PATH."
        Write-Warn "Install Python 3.10+ from https://www.python.org/downloads/ and tick"
        Write-Warn "'Add python.exe to PATH', then run: python -m pip install --user <wheel-url>"
    } else {
        $wheelAsset = $release.assets | Where-Object { $_.name -like "*.whl" } | Select-Object -First 1
        if (-not $wheelAsset) {
            Write-Warn "Release $tag has no wheel asset; skipping the Python control server"
        } else {
            & $python.Source -m pip install --user $wheelAsset.browser_download_url
            if ($LASTEXITCODE -ne 0) {
                Write-Warn "pip install failed; run it manually:"
                Write-Warn "  python -m pip install --user $($wheelAsset.browser_download_url)"
            } else {
                Write-Ok "Installed $($wheelAsset.name)"
            }
        }
    }
} else {
    Write-Warn "Skipped the Python control server (-NoPython)"
}

Write-Host ""
Write-Host "gitar installation finished." -ForegroundColor Green
Write-Host "Next steps:"
Write-Host "  1. Open a new terminal so the PATH change takes effect."
if ($NoPython) {
    Write-Host "  2. Install the control server when you are ready:"
    Write-Host "     python -m pip install --user <gitar-wheel-url>"
} else {
    Write-Host "  2. Start the server and web UI:"
    Write-Host "     gitard serve --open"
}
