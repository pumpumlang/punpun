$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$Version = (Get-Content (Join-Path $Root "VERSION") -Raw).Trim()
$ProductVersion = $Version.Split("-", 2)[0]
$Payload = Join-Path $PSScriptRoot "payload"
if (!(Test-Path $Payload)) { throw "Stage a Windows x64 PunPun SDK at installers/windows/payload first." }
if (!(Get-Command wix -ErrorAction SilentlyContinue)) { throw "WiX Toolset v4 'wix' is required." }
$Msi = Join-Path $PSScriptRoot "PunPun-$Version-win-x64.msi"
$Setup = Join-Path $PSScriptRoot "PunPun-Setup-$Version.exe"

# WiX v4's Files element performs deterministic recursive payload inclusion;
# it avoids the removed legacy `heat`/`wix harvest` command boundary.
wix build "$PSScriptRoot\wix\PunPun.wxs" "$PSScriptRoot\wix\Payload.wxs" `
    -d "Payload=$Payload" -d "ProductVersion=$ProductVersion" -arch x64 -o $Msi
wix build "$PSScriptRoot\wix\Bundle.wxs" -ext WixToolset.Bal.wixext `
    -d "MsiPath=$Msi" -d "ProductVersion=$ProductVersion" -arch x64 -o $Setup
Write-Host "Built MSI and graphical Burn bootstrapper."
