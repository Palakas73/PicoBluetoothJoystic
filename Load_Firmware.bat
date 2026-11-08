
@echo off
setlocal EnableExtensions EnableDelayedExpansion
title Pico W - Firmware Loader

set "PROJECT=%~dp0"
if "%~1"=="" (
    echo Usage: Load_Firmware.bat firmware\name.uf2
    echo Available images:
    dir /b "%PROJECT%firmware\*.uf2"
    exit /b 2
)
set "FIRMWARE=%PROJECT%%~1"
set "PICO_DRIVE="
set /a WAIT_COUNT=0

echo ============================================
echo    PICO BLUETOOTH JOYSTICK - FIRMWARE LOADER
echo ============================================
echo.

:: Verify firmware exists
if not exist "%FIRMWARE%" (
    echo [ERROR] Firmware not found:
    echo %FIRMWARE%
    goto :error
)

echo [OK] Firmware found.
echo [INFO] Selected image: %FIRMWARE%
powershell -NoProfile -Command "(Get-FileHash -LiteralPath '%FIRMWARE%' -Algorithm SHA256).Hash"
echo.

:: Wait for Pico W in BOOTSEL mode
echo Connect the Pico W while holding BOOTSEL.
echo Waiting for the RPI-RP2 BOOTSEL drive ...
echo.

:wait_pico
for %%D in (D E F G H I J K L M N O P Q R S T U V W X Y Z) do (
    if not defined PICO_DRIVE call :probe_drive %%D
)
if not defined PICO_DRIVE (
    set /a WAIT_COUNT+=1
    if !WAIT_COUNT! GEQ 60 (
        echo [ERROR] BOOTSEL drive not found within 120 seconds.
        goto :error
    )
    timeout /t 2 /nobreak >nul
    goto :wait_pico
)

:: Verify this is an RP2040 bootloader drive
if not exist "%PICO_DRIVE%\INFO_UF2.TXT" (
    echo [ERROR] Drive %PICO_DRIVE% is not a UF2 bootloader.
    goto :error
)

findstr /I /C:"RPI-RP2" "%PICO_DRIVE%\INFO_UF2.TXT" >nul
if errorlevel 1 (
    echo [ERROR] Drive %PICO_DRIVE% is not recognized as RPI-RP2.
    goto :error
)

echo [OK] Raspberry Pi Pico detected on %PICO_DRIVE%
echo.

:: Copy firmware
echo [INFO] Flashing firmware...
copy /Y "%FIRMWARE%" "%PICO_DRIVE%\" >nul

if errorlevel 1 (
    echo [ERROR] Firmware copy failed.
    goto :error
)

echo.
echo [OK] Firmware transferred successfully.
echo [INFO] The Pico W should reboot automatically.
echo.
pause
exit /b 0

:error
echo.
echo [FAILED] Firmware was not loaded.
pause
exit /b 1

:probe_drive
if not exist "%~1:\INFO_UF2.TXT" exit /b 0
findstr /I /C:"RPI-RP2" "%~1:\INFO_UF2.TXT" >nul
if not errorlevel 1 set "PICO_DRIVE=%~1:"
exit /b 0
