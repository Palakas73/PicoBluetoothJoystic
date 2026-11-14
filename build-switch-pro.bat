@echo off
setlocal
for %%I in ("%~dp0.") do set "TASK_DIR=%%~fI"
call "%TASK_DIR%\build-autofire-guide.bat"
if errorlevel 1 exit /b %errorlevel%
copy /Y "%TASK_DIR%\build-autofire-guide\pico_bluetooth_joystick.uf2" "%TASK_DIR%\firmware\pico_xinput_dual_switch_pro_candidate.uf2" >nul
if errorlevel 1 exit /b %errorlevel%
if exist "%TASK_DIR%\firmware\pico_xinput_dual_autofire_guide_validated.uf2" copy /Y "%TASK_DIR%\firmware\pico_xinput_dual_autofire_guide_validated.uf2" "%TASK_DIR%\firmware\pico_xinput_dual_autofire_guide_candidate.uf2" >nul
echo [OK] Experimental Nintendo Switch Pro candidate generated.
