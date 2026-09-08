$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Payload = Join-Path $PSScriptRoot "payload"
if (!(Test-Path $Payload)) { throw "Stage a Windows x64 PunPun SDK at installers/windows/payload first." }
if (!(Get-Command wix -ErrorAction SilentlyContinue)) { throw "WiX Toolset v4 'wix' is required." }
# Harvest the staged SDK so every file becomes a proper MSI component.
wix harvest directory $Payload -o "$PSScriptRoot\wix\Payload.wxs" -cg PunPunSdkFiles -dr INSTALLFOLDER -var var.Payload
wix build "$PSScriptRoot\wix\PunPun.wxs" "$PSScriptRoot\wix\Payload.wxs" -d Payload=$Payload -arch x64 -o "$PSScriptRoot\PunPun-0.5.0-beta-win-x64.msi"
wix build "$PSScriptRoot\wix\Bundle.wxs" -ext WixToolset.Bal.wixext -arch x64 -o "$PSScriptRoot\PunPun-Setup-0.5.0-beta.exe"
Write-Host "Built MSI and graphical Burn bootstrapper."
