# Technical references

References explain protocol research and design decisions. Listing a URL does not mean its source code was copied or that it grants a reuse licence.

| Source | Version / file reviewed | Use in this project | Classification |
| --- | --- | --- | --- |
| [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) | 2.3.1, `079c6f39023649b154152db30f1d781e884879bc` | Pico W SDK, CYW43, BTstack and TinyUSB integration | Dependency |
| [pico-examples](https://github.com/raspberrypi/pico-examples) | Bluetooth examples matching SDK APIs | Pico W initialization and BTstack event-loop patterns | Conceptual/reference |
| [BTstack](https://github.com/bluekitchen/btstack) | `eb0bb8b5ea6d234ccb940313b47f7a5c3b4e20ec`; `example/hid_host_demo.c`, HOGP/HID APIs | BLE HOGP and Classic HID host event sequencing | Dependency and API reference |
| [TinyUSB](https://github.com/hathach/tinyusb) | `86ad6e56c1700e85f1c5678607a762cfe3aa2f47` | USB device callbacks, custom class driver, MSC | Dependency and API reference |
| [Joypad OS](https://github.com/joypad-ai/joypad-os) | audited commit `9fa79f457ae94286d43da7602ab32137303cd7d6`; Xbox BLE/Classic drivers | Compared controller mapping and transport architecture | Reference only; no source incorporated |
| [GP2040-CE](https://github.com/OpenStickCommunity/GP2040-CE) | audited commit `0cc9061d2352e4b9fe7b069d4e9698c10c8ff0a0` | Compared RP2040 TinyUSB custom XInput architecture | Reference only; no source incorporated |
| [Buddycon / OGX-Mini firmware](https://github.com/raygan/buddycon-firmware) | audited commit `33c7aad9d15d4f5e0daf42f0147604aa455e8922` | Compared XInput report and endpoint handling | Reference only; no source incorporated |
| [Nintendo Switch reverse-engineering notes](https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering) | `bluetooth_hid_notes.md`, `bluetooth_hid_subcommands_notes.md` | 8BitDo Switch-mode initialization and report hypotheses, then checked against real `0x3F` captures | Factual protocol reference |
| [Linux `hid-nintendo`](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-nintendo.c) | concrete driver file consulted | Calibration, report IDs, and Switch controller behavior comparison | GPL-2.0-only reference; no code incorporated |
| [MS-XUSBI](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-xusbi/) | XUSB interfaces, reports, adapter interfaces, OS compatible IDs | XUSB descriptor/report interoperability | Official protocol specification |
| [Microsoft XInput documentation](https://learn.microsoft.com/en-us/windows/win32/xinput/xinput-game-controller-apis-portal) | Windows XInput API documentation | Windows validation tool and terminology | Official documentation |
| [8BitDo Ultimate 2C Bluetooth support](https://support.8bitdo.com/faq/ultimate-2c-bluetooth-controller.html) | product FAQ/manual pages | User pairing procedure and advertised Switch mode | Official product documentation |
| [Raspberry Pi USB PID registry](https://github.com/raspberrypi/usb-pid) | current registry; `000A` entry | Verified that development PID `2E8A:000A` is allocated to Pico SDK CDC UART | Official registry |
| [Community Xbox BLE descriptor capture](https://gist.github.com/adzierzanowski/948d56f6d260d7c1f316a0a88322dd84) | HID map/report data for `045e:0b13` | Initial parser fixture, not proof for every firmware revision | Unlicensed factual capture; provenance caution |

## Findings grounded in physical evidence

- Xbox model 1914 firmware 5.9.2709.0 was physically validated by the user with this project over BLE.
- The 8BitDo's captured Classic HID traffic uses HIDP `A1`, Report ID `0x3F`, 13-byte framed reports, and 16-bit little-endian axes. This device evidence superseded the initial Switch Pro `0x30` assumption.
- The user confirmed both controllers operating simultaneously through two XInput outputs, reconnection, slot behavior, 8BitDo player LED, AUTOFIRE, and the read-only guide drive.

No latency percentile, endurance duration, or console compatibility is inferred from these observations.
