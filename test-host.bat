@echo off
setlocal
for %%I in ("%~dp0.") do set "TASK_DIR=%%~fI"
if not defined INCLUDE call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -no_logo -arch=amd64
cmake -S "%TASK_DIR%" -B "%TASK_DIR%\build-host-tests" -G Ninja -DBUILD_HOST_TESTS=ON
if errorlevel 1 exit /b %errorlevel%
cmake --build "%TASK_DIR%\build-host-tests"
if errorlevel 1 exit /b %errorlevel%
ctest --test-dir "%TASK_DIR%\build-host-tests" --output-on-failure
