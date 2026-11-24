# Builds the plugin with Visual Studio and packages the Inno Setup installer
# into release/. Used by CI; also runs locally from PowerShell.
#
# Needs Visual Studio with the C++ workload (its bundled CMake is used) and
# Inno Setup 6. The first configure downloads OBS dependencies into .deps/.

[CmdletBinding()]
param(
    [string] $Configuration = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Resolve-Path -Path "$PSScriptRoot/../.."
Set-Location $ProjectRoot

function Invoke-Checked {
    param([string] $Command, [string[]] $Arguments)
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Command $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
    }
}

# Any Visual Studio whose C++ tools are installed; use its bundled CMake.
$VsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $VsWhere)) { throw 'vswhere.exe not found; install Visual Studio with the C++ workload' }
$VsDir = & $VsWhere -all -products * -property installationPath |
    Where-Object { Test-Path "$_\VC\Auxiliary\Build\vcvars64.bat" } | Select-Object -First 1
if (-not $VsDir) { throw 'No Visual Studio with the C++ x64 tools' }
$env:PATH = "$VsDir\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;$env:PATH"
Write-Host "Visual Studio: $VsDir"

# Inno Setup: the uninstall registry entry (per-machine or any user's), the
# default install folders, or PATH. The runner service often runs as
# another account than the one that installed Inno Setup.
$IsccCandidates = @()
$UninstallKeys = @(
    'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1',
    'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1'
)
if (Test-Path 'Registry::HKEY_USERS') {
    $UninstallKeys += Get-ChildItem 'Registry::HKEY_USERS' -ErrorAction SilentlyContinue |
        ForEach-Object { "Registry::$($_.Name)\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1" }
}
foreach ($Key in $UninstallKeys) {
    $Location = (Get-ItemProperty -Path $Key -ErrorAction SilentlyContinue).InstallLocation
    if ($Location) { $IsccCandidates += Join-Path $Location 'ISCC.exe' }
}
$IsccCandidates += @(
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
)
$IsccCandidates += Get-ChildItem 'C:\Users\*\AppData\Local\Programs\Inno Setup 6\ISCC.exe' -ErrorAction SilentlyContinue |
    ForEach-Object { $_.FullName }
$Iscc = $IsccCandidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $Iscc) { $Iscc = (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source }
if (-not $Iscc) {
    Write-Host "Looked for ISCC.exe in:`n  $($IsccCandidates -join "`n  ")"
    throw 'ISCC.exe not found. Install Inno Setup 6 for all users: winget install JRSoftware.InnoSetup --scope machine'
}
Write-Host "Inno Setup: $Iscc"

# OBS dependency archives: damaged ones replaced, missing ones taken from
# OBS_DEPS_MIRROR when set.
Invoke-Checked cmake @('-DPLATFORM=windows-x64', '-P', "$PSScriptRoot/prefetch-deps.cmake")

# URLs from the CI variables, always passed so a value left in the CMake cache
# (such as a local test update server) never ships. Unset API URL: the mock
# backend; unset manifest URL: no update checks.
Invoke-Checked cmake @('--preset', 'windows-x64', '-DCMAKE_COMPILE_WARNING_AS_ERROR=ON', "-DLANGLIVE_UPDATE_MANIFEST_URL=$env:LANGLIVE_UPDATE_MANIFEST_URL",
    "-DLANGLIVE_API_BASE_URL=$env:LANGLIVE_API_BASE_URL", "-DLANGLIVE_WEBSITE_URL=$env:LANGLIVE_WEBSITE_URL",
    "-DLANGLIVE_QA_BUILD=$env:LANGLIVE_QA_BUILD")
Invoke-Checked cmake @('--build', '--preset', 'windows-x64', '--config', $Configuration)

$InstallDir = Join-Path $ProjectRoot 'install'
if (Test-Path $InstallDir) { Remove-Item -Recurse -Force $InstallDir }
Invoke-Checked cmake @('--install', 'build_x64', '--config', $Configuration, '--prefix', $InstallDir)

& "$PSScriptRoot/../windows/package-windows-installer.ps1" -Configuration $Configuration -IsccPath $Iscc
