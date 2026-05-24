#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Builds rainmeter-gamemode-activator and places the executable in dist/.
.DESCRIPTION
    Reads version from version.txt, detects win32/win64 from the compiler,
    compiles with MinGW gcc, and outputs to:
        dist/rainmeter-gamemode-activator-<version>-<arch>.exe
.PARAMETER SkipClean
    Skip removing the dist/ directory before building.
.PARAMETER NoStrip
    Build without -O2 optimisation.
#>
param(
    [switch]$SkipClean,
    [switch]$NoStrip
)

$ErrorActionPreference = "Stop"

# ── Config ──────────────────────────────────────────────────────────────────
$versionFile   = "version.txt"
$resFile       = "resources.rc"
$resOutput     = "resources.o"
$srcDir        = "src"
$distDir       = "dist"

# ── Read version ────────────────────────────────────────────────────────────
if (-not (Test-Path -Path $versionFile)) {
    Write-Error "version.txt not found"
    exit 1
}
$version = (Get-Content -Path $versionFile -Raw).Trim()
if (-not $version) {
    Write-Error "version.txt is empty"
    exit 1
}
Write-Host "Version: $version"

# ── Detect target architecture ──────────────────────────────────────────────
$arch = "win64"
$machine = & gcc -dumpmachine 2>&1
if ($LASTEXITCODE -eq 0) {
    if ($machine -like "*w64*") {
        $arch = "win64"
    } elseif ($machine -like "*mingw32*" -or $machine -like "*i686*") {
        $arch = "win32"
    }
}
Write-Host "Arch  : $arch"

# ── Compile resources ───────────────────────────────────────────────────────
Write-Host "Resource: $resFile"
$resTarget = if ($arch -eq "win64") { "pe-x86-64" } else { "pe-i386" }
& windres --target=$resTarget -i $resFile -o $resOutput
if ($LASTEXITCODE -ne 0) {
    Write-Error "Resource compilation failed"
    exit 1
}

# ── Build main executable ───────────────────────────────────────────────────
$targetName = "rainmeter-gamemode-activator-$version-$arch.exe"
$targetPath = Join-Path -Path $distDir -ChildPath $targetName
$markerName = "gamemode_active.exe"
$markerPath = Join-Path -Path $distDir -ChildPath $markerName

if (-not $SkipClean -and (Test-Path -Path $distDir)) {
    Write-Host "Cleaning dist/ ..."
    Remove-Item -Path $distDir -Recurse -Force
}
if (-not (Test-Path -Path $distDir)) {
    New-Item -ItemType Directory -Path $distDir | Out-Null
}

$cflags = @("-Wall", "-Wextra", "-std=c11", "-I.")
if (-not $NoStrip) {
    $cflags += "-O2"
}

# Marker process (gamemode_active.exe)
Write-Host "Compiling: $markerName"
& gcc -O2 -std=c11 -mwindows -o $markerPath (Join-Path $srcDir "gamemode_active.c")
if ($LASTEXITCODE -ne 0) {
    Write-Error "Marker build failed"
    exit 1
}
$markerItem = Get-Item -Path $markerPath
Write-Host "  OK -> $markerPath ($($markerItem.Length) bytes)"

# Main process
Write-Host ""
Write-Host "Compiling: $targetName"
Write-Host ""

& gcc @cflags "-DVERSION=`"$version`"" -o $targetPath `
    (Join-Path $srcDir "main.c") `
    (Join-Path $srcDir "detector.c") `
    (Join-Path $srcDir "logger.c") `
    $resOutput `
    -lgdi32 -lpsapi -mwindows

if ($LASTEXITCODE -ne 0) {
    Write-Error "Main build failed"
    exit 1
}

Write-Host ""
Write-Host "OK -> $targetPath"

# ── Cleanup temp files ──────────────────────────────────────────────────────
Remove-Item -Path $resOutput -Force -ErrorAction SilentlyContinue

# ── Show file info ──────────────────────────────────────────────────────────
$item = Get-Item -Path $targetPath
Write-Host "Size: $($item.Length) bytes"

# ── Package ZIP ─────────────────────────────────────────────────────────────
$zipName = "rainmeter-gamemode-activator-$version-$arch.zip"
$zipPath = Join-Path -Path $distDir -ChildPath $zipName
$exclude = @("*.zip")
$files = Get-ChildItem -Path $distDir -Exclude $exclude
Compress-Archive -Path $files -DestinationPath $zipPath -Force
Write-Host ""
Write-Host "ZIP -> $zipPath"
