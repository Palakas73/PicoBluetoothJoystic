$ErrorActionPreference = 'Stop'
$Repo = $PSScriptRoot
$ThirdParty = Join-Path $Repo 'third_party'
New-Item -ItemType Directory -Force -Path $ThirdParty | Out-Null

function Get-VerifiedArchive([string]$Url, [string]$Path, [string]$Sha256) {
    if (-not (Test-Path -LiteralPath $Path)) {
        Invoke-WebRequest -Uri $Url -OutFile $Path
    }
    $Actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    if ($Actual -ne $Sha256) { throw "SHA256 mismatch for $Path (got $Actual)" }
}

$Sdk = Join-Path $ThirdParty 'pico-sdk'
if (-not (Test-Path -LiteralPath $Sdk)) {
    git clone --branch 2.3.1 --depth 1 --recurse-submodules https://github.com/raspberrypi/pico-sdk.git $Sdk
    if ($LASTEXITCODE -ne 0) { throw 'Pico SDK clone failed' }
}
git -C $Sdk submodule update --init --recursive
if ($LASTEXITCODE -ne 0) { throw 'Pico SDK submodule initialization failed' }
$SdkCommit = (git -C $Sdk rev-parse HEAD).Trim()
if ($SdkCommit -ne '079c6f39023649b154152db30f1d781e884879bc') { throw "Unexpected Pico SDK revision: $SdkCommit" }

$LlvmArchive = Join-Path $ThirdParty 'LLVMEmbeddedToolchainForArm-14.0.0-windows.zip'
Get-VerifiedArchive 'https://github.com/ARM-software/LLVM-embedded-toolchain-for-Arm/releases/download/release-14.0.0/LLVMEmbeddedToolchainForArm-14.0.0-windows.zip' $LlvmArchive '1D0BA47D93E09E34EE1E31CEC43F221B9D689401A5EAE568DF6CDEF0261C6ABB'
$LlvmDir = Join-Path $ThirdParty 'arm-llvm-14'
if (-not (Test-Path (Join-Path $LlvmDir 'LLVMEmbeddedToolchainForArm-14.0.0\bin\clang.exe'))) {
    Expand-Archive -LiteralPath $LlvmArchive -DestinationPath $LlvmDir -Force
}

$ToolsArchive = Join-Path $ThirdParty 'pico-sdk-tools-2.3.1-x64-win.zip'
Get-VerifiedArchive 'https://github.com/raspberrypi/pico-sdk-tools/releases/download/v2.3.1-0/pico-sdk-tools-2.3.1-x64-win.zip' $ToolsArchive 'C5D18DAE371EB088DA61A038CCF56A23FBD19171A319461EFEBEAE1646C06ADE'
$ToolsDir = Join-Path $ThirdParty 'pico-sdk-tools-2.3.1'
if (-not (Test-Path (Join-Path $ToolsDir 'pioasm\pioasm.exe'))) {
    Expand-Archive -LiteralPath $ToolsArchive -DestinationPath $ToolsDir -Force
}

$PicotoolArchive = Join-Path $ThirdParty 'picotool-2.3.1-x64-win.zip'
Get-VerifiedArchive 'https://github.com/raspberrypi/pico-sdk-tools/releases/download/v2.3.1-0/picotool-2.3.1-x64-win.zip' $PicotoolArchive '68730BE0813F8F35BE2CCA147CF7F1572662D5DCF0F5BA468E02A6DD9E85DB2B'
$PicotoolDir = Join-Path $ThirdParty 'picotool-2.3.1'
if (-not (Test-Path (Join-Path $PicotoolDir 'picotool\picotool.exe'))) {
    Expand-Archive -LiteralPath $PicotoolArchive -DestinationPath $PicotoolDir -Force
}

Write-Host 'Dependencies installed locally under third_party. No global PATH or toolchain settings were changed.'
Write-Host 'Next: run build.bat. For tests: run test-host.bat from a Visual Studio 2022 developer command prompt.'
