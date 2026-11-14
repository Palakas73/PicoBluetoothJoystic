# Third-party notices and provenance

This inventory is based on the source tree, pinned local dependencies, build files, and repository history reviewed on 2026-11-08. It is a technical provenance record, not legal advice or a guarantee of compliance.

| Component | Pinned version | Relationship | Licence / obligations | Evidence and local modifications |
| --- | --- | --- | --- | --- |
| Raspberry Pi Pico SDK | 2.3.1, `079c6f39023649b154152db30f1d781e884879bc` | Build and runtime dependency, downloaded by `setup-deps.ps1`; not vendored | 3-clause BSD. Preserve notice for source and binary redistribution. | `cmake/pico_sdk_import.cmake` is the SDK import helper and retains Raspberry Pi's full header. Project CMake integration only. |
| BTstack | `eb0bb8b5ea6d234ccb940313b47f7a5c3b4e20ec` through Pico SDK | Linked Bluetooth stack; not vendored | The Pico SDK 2.3.1 RP licence permits customers to use, modify, and distribute BTstack as part of Pico W/Pico WH/Pico 2 W/Pico 2 WH/RM2 products and related SDK/documentation, limited to those products. Upstream's general licence separately restricts commercial use. Preserve the applicable text and do not imply sublicensing beyond it. | Public project code calls BTstack APIs. No BTstack source is copied into this repository. See `LICENSES/BTSTACK-RASPBERRY-PI.txt`. |
| TinyUSB | `86ad6e56c1700e85f1c5678607a762cfe3aa2f47` through Pico SDK | Linked USB stack; not vendored | MIT; include copyright and permission notice with substantial source/binary distributions. | Local code implements custom TinyUSB class callbacks and descriptors; no upstream TinyUSB source copied. |
| CYW43 driver | `055d64274b014dd7b1c2fc94d26e8a18face7124` through Pico SDK | Linked radio driver; not vendored | `LICENSE.RP` permits use and redistribution only with RP2040 or other Raspberry Pi semiconductor devices, with notice/disclaimer preservation. | Used only for Pico W/CYW43439 through Pico SDK. |
| Arm LLVM Embedded Toolchain | 14.0.0 | Build tool downloaded locally; not distributed | LLVM/toolchain component licences. Users obtain it from Arm's release. | No toolchain files in Git. |
| `pico_sdk_import.cmake` | from Raspberry Pi Pico SDK | Incorporated build helper | 3-clause BSD header retained in the file. | No removal of the original notice. |

## Project-authored code

Files under `src/`, `tests/`, `tools/`, project documentation, and guide assets were developed for this repository unless a file retains a different notice. Project-authored code is offered under `LICENSE` (MIT). That licence does not relicense dependencies, specifications, trademarks, or third-party material.

The XUSB implementation was developed against the public MS-XUSBI specification and by comparing architecture with open projects. The Switch Pro initialization was implemented from protocol facts cross-checked against dekuNukem's notes and Linux `drivers/hid/hid-nintendo.c`. The repository review found no copied GP2040-CE, Buddycon/OGX-Mini, Joypad OS, Linux `hid-nintendo`, or dekuNukem source file. Those are technical references, not incorporated dependencies; in particular, no GPL kernel implementation was copied or translated.

DualShock 4 protocol facts are cross-checked against Bluepad32's Apache-2.0 `uni_hid_parser_ds4.c` and Linux's GPL-2.0-or-later `drivers/hid/hid-playstation.c`; neither file's code is copied or translated into this repository. Sony pairing instructions are linked as user documentation. These references do not add Bluepad32 or Linux code as dependencies and do not transfer their licences to project-authored code. See `REFERENCES.md` for the specific facts used and validation limits.

## Binary redistribution checklist

A firmware binary links Pico SDK, BTstack, TinyUSB, and CYW43 code. A distributor must:

1. Preserve the applicable notices and licence texts with the binary.
2. Use BTstack only within the Pico-family scope granted by `LICENSES/BTSTACK-RASPBERRY-PI.txt`, or obtain other terms from BlueKitchen.
3. Use the CYW43 driver only with permitted Raspberry Pi semiconductor devices under `LICENSES/CYW43-RASPBERRY-PI.txt`, or obtain other terms.
4. Use an authorized USB VID/PID. The current development `2E8A:000A` is not assigned to this project.
5. Avoid implying endorsement by any referenced vendor.

For those reasons the public repository does not publish the current development UF2 as a GitHub Release.

## Pending provenance questions

- The public Xbox BLE HID fixture cited in `REFERENCES.md` is represented as test data values rather than copied source, but its gist has no explicit licence. Keep it confined to factual interoperability data and do not claim a licence grant.
- Microsoft specifications permit implementation for interoperability; Microsoft names and protocol identifiers remain their owners' marks/material. No Microsoft VID/PID is used.
- The instructional `AUTOFIRE.PNG` was supplied for this project. It is identified as an illustration and is not represented as official vendor artwork.
