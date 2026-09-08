$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Payload = Join-Path $PSScriptRoot "payload"
if (!(Test-Path $Payload)) { throw "Stage a Windows x64 PunPun SDK at installers/windows/payload first." }
if (!(Get-Command wix -ErrorAction SilentlyContinue)) { throw "WiX Toolset v4 'wix' is required." }
$Msi = Join-Path $PSScriptRoot "PunPun-0.5.0-beta-win-x64.msi"
$Setup = Join-Path $PSScriptRoot "PunPun-Setup-0.5.0-beta.exe"

# WiX v4's Files element performs deterministic recursive payload inclusion;
# it avoids the removed legacy `heat`/`wix harvest` command boundary.
wix build "$PSScriptRoot\wix\PunPun.wxs" "$PSScriptRoot\wix\Payload.wxs" `
    -d "Payload=$Payload" -arch x64 -o $Msi
wix build "$PSScriptRoot\wix\Bundle.wxs" -ext WixToolset.Bal.wixext `
    -d "MsiPath=$Msi" -arch x64 -o $Setup
Write-Host "Built MSI and graphical Burn bootstrapper."
