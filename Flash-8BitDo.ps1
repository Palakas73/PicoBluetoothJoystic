$ErrorActionPreference = 'Stop'

$project = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildBat = Join-Path $project 'build-8bitdo-xinput.bat'
$loadBat = Join-Path $project 'Load_Firmware.bat'

if (-not (Test-Path -LiteralPath $buildBat)) {
    throw "File not found: $buildBat"
}
if (-not (Test-Path -LiteralPath $loadBat)) {
    throw "File not found: $loadBat"
}

Write-Host '=== Building 8BitDo Classic HID -> XInput firmware ===' -ForegroundColor Cyan
& cmd.exe /c $buildBat
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE."
}

Write-Host ''
Write-Host '=== Preparing BOOTSEL upload ===' -ForegroundColor Cyan
Write-Host 'Disconnect Pico, hold BOOTSEL, and reconnect it now.' -ForegroundColor Yellow
Write-Host 'The loader will detect the RPI-RP2 drive automatically.' -ForegroundColor Yellow
& cmd.exe /c $loadBat 'firmware\pico_xinput_8bitdo_candidate.uf2'
if ($LASTEXITCODE -ne 0) {
    throw "Upload failed with exit code $LASTEXITCODE."
}

Write-Host ''
Write-Host '8BitDo firmware loaded successfully.' -ForegroundColor Green
