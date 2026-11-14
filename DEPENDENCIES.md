# Windows build dependencies

| Component | Version / revision | Local path |
| --- | --- | --- |
| Raspberry Pi Pico SDK | 2.3.1, `079c6f39023649b154152db30f1d781e884879bc` | `third_party/pico-sdk` |
| BTstack | `eb0bb8b5ea6d234ccb940313b47f7a5c3b4e20ec` | `third_party/pico-sdk/lib/btstack` |
| TinyUSB | `86ad6e56c1700e85f1c5678607a762cfe3aa2f47` | `third_party/pico-sdk/lib/tinyusb` |
| CYW43 driver | `055d64274b014dd7b1c2fc94d26e8a18face7124` | `third_party/pico-sdk/lib/cyw43-driver` |
| Arm LLVM Embedded Toolchain | 14.0.0 Windows x64; SHA-256 `1D0BA47D93E09E34EE1E31CEC43F221B9D689401A5EAE568DF6CDEF0261C6ABB` | `third_party/arm-llvm-14/LLVMEmbeddedToolchainForArm-14.0.0` |
| Pico SDK tools | 2.3.1 Windows x64; SHA-256 `C5D18DAE371EB088DA61A038CCF56A23FBD19171A319461EFEBEAE1646C06ADE` | `third_party/pico-sdk-tools-2.3.1` |
| picotool | 2.3.1 Windows x64; SHA-256 `68730BE0813F8F35BE2CCA147CF7F1572662D5DCF0F5BA468E02A6DD9E85DB2B` | `third_party/picotool-2.3.1/picotool` |
| Host tools used here | CMake 4.4.4, Ninja, Python 3.14.5, Visual Studio 2022 Build Tools | user PATH / standard install |

`setup-deps.ps1` downloads fixed official archives, verifies hashes, and clones Pico SDK 2.3.1 recursively into ignored `third_party/` without changing the global PATH.

```powershell
.\setup-deps.ps1
.\test-host.bat
.\build-autofire-guide.bat
```

The firmware build selects `pico_w`, Cortex-M0+ LLVM, dual BLE/Classic input, two XUSB interfaces, and the immutable MSC guide. Output: `firmware/pico_xinput_dual_autofire_guide_candidate.uf2`.

Restore SDK submodules with:

```powershell
git -C third_party/pico-sdk submodule update --init --recursive
```

Official sources: [Pico SDK](https://github.com/raspberrypi/pico-sdk), [Pico SDK tools](https://github.com/raspberrypi/pico-sdk-tools/releases/tag/v2.3.1-0), [Arm LLVM](https://github.com/ARM-software/LLVM-embedded-toolchain-for-Arm/releases/tag/release-14.0.0), and [picotool](https://github.com/raspberrypi/pico-sdk-tools/releases/tag/v2.3.1-0). See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for licence obligations.
