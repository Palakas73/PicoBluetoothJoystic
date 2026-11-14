@echo off
setlocal
for %%I in ("%~dp0.") do set "TASK_DIR=%%~fI"
set "PICO_SDK_PATH=%TASK_DIR%\third_party\pico-sdk"
set "LLVM_ROOT=%TASK_DIR%\third_party\arm-llvm-14\LLVMEmbeddedToolchainForArm-14.0.0"
set "PATH=%LLVM_ROOT%\bin;%TASK_DIR%\third_party\pico-sdk-tools-2.3.1\pioasm;%TASK_DIR%\third_party\picotool-2.3.1\picotool;%PATH%"
cmake -S "%TASK_DIR%" -B "%TASK_DIR%\build-dualshock4-diagnostic" -G Ninja -DPICO_BOARD=pico_w -DBRIDGE_USB_MODE=HID -DBRIDGE_INPUT_MODE=CLASSIC_8BITDO -DPICO_BRIDGE_VERBOSE=ON -DPICO_COMPILER=pico_arm_cortex_m0plus_clang -DPICO_TOOLCHAIN_PATH="%LLVM_ROOT%\bin" -DPICO_COMPILER_SYSROOT="%LLVM_ROOT%\lib\clang-runtimes\armv6m_soft_nofp" -DPICO_CLIB=newlib -DPICO_SDK_PATH="%PICO_SDK_PATH%" -Dpicotool_DIR="%TASK_DIR%\third_party\picotool-2.3.1\picotool" -DPICO_NO_PICOTOOL=OFF -DCMAKE_EXE_LINKER_FLAGS=-Wl,--threads=1 -DBUILD_HOST_TESTS=OFF
if errorlevel 1 exit /b %errorlevel%
cmake --build "%TASK_DIR%\build-dualshock4-diagnostic"
if errorlevel 1 exit /b %errorlevel%
if not exist "%TASK_DIR%\firmware" mkdir "%TASK_DIR%\firmware"
copy /Y "%TASK_DIR%\build-dualshock4-diagnostic\pico_bluetooth_joystick.uf2" "%TASK_DIR%\firmware\pico_ds4_cdc_diagnostic.uf2" >nul
if errorlevel 1 exit /b %errorlevel%
echo [OK] CDC + HID DS4 diagnostic firmware: firmware\pico_ds4_cdc_diagnostic.uf2
