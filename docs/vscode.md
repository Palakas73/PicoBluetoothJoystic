# Visual Studio Code

Open the repository root. Recommended extensions are C/C++, CMake Tools, and optionally Cortex-Debug. The project uses Pico SDK and CMake directly rather than PlatformIO because the selected Pico W BTstack host combination is verified in the SDK build.

Run `setup-deps.ps1` once, then use the repository `.bat` scripts. They set local SDK/toolchain paths without changing the global PATH. The target board is always `pico_w`.

Host tests use Visual Studio 2022 Build Tools, Ninja, and CTest and do not initialize Pico hardware.
