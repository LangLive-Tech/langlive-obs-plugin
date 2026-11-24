[CmdletBinding()]
param(
    [string] $Configuration = 'RelWithDebInfo',
    [string] $InstallRoot = '',
    [string] $IsccPath = "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Resolve-Path -Path "$PSScriptRoot/../.."
# Defaults that use $PSScriptRoot are set here: Windows PowerShell 5.1 leaves
# it empty in param() defaults.
if (-not $InstallRoot) { $InstallRoot = "$ProjectRoot/install/langlive-obs-plugin" }
$BuildSpec = Get-Content -Path "$ProjectRoot/buildspec.json" -Raw -Encoding UTF8 | ConvertFrom-Json

if (-not (Test-Path $InstallRoot)) {
    throw "Install root not found: $InstallRoot. Run 'cmake --install build_x64 --config $Configuration --prefix install' first."
}

if (-not (Test-Path $IsccPath)) {
    throw "ISCC.exe not found at $IsccPath. Install Inno Setup (winget install JRSoftware.InnoSetup)."
}

$OutputDir = "$ProjectRoot/release"
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$OutputBasename = "$($BuildSpec.name)-windows-x64-v$($BuildSpec.version)"

# Read as UTF-8 explicitly: Windows PowerShell 5.1 otherwise uses the ANSI
# code page and garbles the Chinese messages in the [Code] section.
$IssTemplate = Get-Content -Path "$ProjectRoot/cmake/windows/resources/installer.iss.in" -Raw -Encoding UTF8
$Iss = $IssTemplate `
    -replace '@DISPLAY_NAME@', $BuildSpec.displayName `
    -replace '@PLUGIN_VERSION@', $BuildSpec.version `
    -replace '@PLUGIN_AUTHOR@', $BuildSpec.author `
    -replace '@PRODUCT_NAME@', $BuildSpec.name `
    -replace '@OUTPUT_DIR@', $OutputDir.Replace('\', '\\') `
    -replace '@OUTPUT_BASENAME@', $OutputBasename `
    -replace '@INSTALL_ROOT@', (Resolve-Path $InstallRoot).Path.Replace('\', '\\')

$IssPath = "$ProjectRoot/release/installer.iss"
Set-Content -Path $IssPath -Value $Iss -Encoding UTF8

& $IsccPath $IssPath
if ($LASTEXITCODE -ne 0) {
    throw "ISCC.exe failed with exit code $LASTEXITCODE"
}

Write-Host "Installer written to $OutputDir/$OutputBasename.exe"
