param(
    [Parameter(Mandatory=$true)][ValidatePattern('^[0-9]+$')][string]$SerialNumber,
    [string]$Nrfutil = '',
    [string]$FirmwareDirectory = ''
)
$ErrorActionPreference = 'Stop'
if (!$Nrfutil) { $Nrfutil = Join-Path $PSScriptRoot 'nrfutil.ps1' }
if (!$FirmwareDirectory) {
    $scriptRoot = Split-Path -Parent $PSScriptRoot
    if (Test-Path -LiteralPath (Join-Path $scriptRoot 'BUILD.json')) { $FirmwareDirectory = $scriptRoot }
    else { $FirmwareDirectory = Join-Path $scriptRoot 'dist/nrf5340dk' }
}
$receipt = Get-Content -LiteralPath (Join-Path $FirmwareDirectory 'BUILD.json') -Raw | ConvertFrom-Json
foreach ($name in @('minimic_app.hex','minimic_net.hex')) {
    $actual = (Get-FileHash -LiteralPath (Join-Path $FirmwareDirectory $name) -Algorithm SHA256).Hash
    if ($actual -ne $receipt.artifacts.$name) { throw "Firmware checksum mismatch: $name" }
}
Write-Host "Replacing both core firmwares on nRF5340 DK $SerialNumber. Keep USB J2 connected."
# Program network first, without resetting into a mixed-version pair of images.
& $Nrfutil device program --serial-number $SerialNumber --family nrf53 --core network `
    --firmware (Join-Path $FirmwareDirectory 'minimic_net.hex') `
    --options 'chip_erase_mode=ERASE_ALL,verify=VERIFY_READ,reset=RESET_NONE'
if ($LASTEXITCODE -ne 0) { throw 'Network core programming failed; application core was not programmed.' }
& $Nrfutil device program --serial-number $SerialNumber --family nrf53 --core application `
    --firmware (Join-Path $FirmwareDirectory 'minimic_app.hex') `
    --options 'chip_erase_mode=ERASE_ALL,verify=VERIFY_READ,reset=RESET_SYSTEM'
if ($LASTEXITCODE -ne 0) { throw 'Application core programming failed. Re-run after correcting the reported problem.' }
Write-Host 'Both cores programmed and verified. Connect nRF USB J3 for audio.'
