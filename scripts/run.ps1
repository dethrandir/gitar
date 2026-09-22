#Requires -Version 5.1
<#
.SYNOPSIS
    Builds the gitar v2 engine (if needed), sets up the venv, and serves the web UI.
.DESCRIPTION
    Developer convenience runner for the v2 stack. Reuses an existing engine
    binary and virtual environment, so a second run is fast.
.PARAMETER Rebuild
    Remove engine\build and configure+build from scratch.
.PARAMETER NoOpen
    Do not open the web UI in a browser.
.PARAMETER NoEngine
    Skip building the engine (use an existing binary).
.PARAMETER GitardArgs
    Extra arguments forwarded to 'gitard serve' (typically after --).
.EXAMPLE
    ./scripts/run.ps1
.EXAMPLE
    ./scripts/run.ps1 -NoEngine -NoOpen -- --port 8080
#>
[CmdletBinding()]
param(
    [switch]$Rebuild,
    [switch]$NoOpen,
    [switch]$NoEngine,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$GitardArgs
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$EngineBuild = Join-Path $Root "engine\build"
$EngineExe = Join-Path $EngineBuild "Release\gitar-engine.exe"
$VenvDir = Join-Path $Root ".venv"
$VenvPython = Join-Path $VenvDir "Scripts\python.exe"
$VenvGitard = Join-Path $VenvDir "Scripts\gitard.exe"

function Write-Step {
    param([string]$Message)
    Write-Host "==> $Message" -ForegroundColor Cyan
}

Write-Host "gitar developer runner" -ForegroundColor White

if (-not $NoEngine) {
    if ($Rebuild -and (Test-Path $EngineBuild)) {
        Write-Step "Removing engine\build for a clean rebuild"
        Remove-Item -Path $EngineBuild -Recurse -Force
    }
    if (Test-Path $EngineExe) {
        Write-Step "Engine already built: $EngineExe"
    } else {
        if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
            throw "cmake is required to build the engine but was not found on PATH."
        }
        Write-Step "Configuring the engine (Visual Studio 17 2022, x64)"
        cmake -S (Join-Path $Root "engine") -B $EngineBuild -G "Visual Studio 17 2022" -A x64
        if ($LASTEXITCODE -ne 0) { throw "cmake configure failed with exit code $LASTEXITCODE." }
        Write-Step "Building the engine (Release)"
        cmake --build $EngineBuild --config Release
        if ($LASTEXITCODE -ne 0) { throw "cmake build failed with exit code $LASTEXITCODE." }
    }
    if (-not $env:GITAR_ENGINE_BIN) {
        $env:GITAR_ENGINE_BIN = $EngineExe
    }
} else {
    Write-Step "Skipping the engine build (-NoEngine)"
}

if (-not (Test-Path $VenvDir)) {
    $python = Get-Command python -ErrorAction SilentlyContinue
    if (-not $python) {
        throw "python was not found on PATH; install Python 3.10+ and retry."
    }
    Write-Step "Creating the Python virtual environment (.venv)"
    & $python.Source -m venv $VenvDir
    if ($LASTEXITCODE -ne 0) { throw "python -m venv failed with exit code $LASTEXITCODE." }
}

if (-not (Test-Path $VenvGitard)) {
    Write-Step "Installing the control server into .venv"
    & $VenvPython -m pip install --quiet --upgrade pip
    if ($LASTEXITCODE -ne 0) { throw "pip upgrade failed with exit code $LASTEXITCODE." }
    Push-Location $Root
    try {
        & $VenvPython -m pip install --quiet -e ".[dev]"
        if ($LASTEXITCODE -ne 0) { throw "pip install failed with exit code $LASTEXITCODE." }
    } finally {
        Pop-Location
    }
} else {
    Write-Step "Control server already installed in .venv"
}

# PowerShell may keep the POSIX '--' separator in the remaining arguments.
if ($GitardArgs -and $GitardArgs.Count -ge 1 -and $GitardArgs[0] -eq "--") {
    if ($GitardArgs.Count -gt 1) {
        $GitardArgs = $GitardArgs[1..($GitardArgs.Count - 1)]
    } else {
        $GitardArgs = @()
    }
}

$serveArgs = @("serve")
if (-not $NoOpen) {
    $serveArgs += "--open"
}
if ($GitardArgs) {
    $serveArgs += $GitardArgs
}
Write-Step "Starting the web UI (gitard serve)"
& $VenvGitard @serveArgs
