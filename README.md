# PicoBluetoothJoystic

PicoBluetoothJoystic turns a Raspberry Pi Pico W into a two-player Bluetooth-to-USB gamepad bridge for Windows. It accepts one Xbox Series controller over BLE and one 8BitDo Ultimate 2C Bluetooth controller in Switch mode over Bluetooth Classic, normalizes both inputs, and exposes two independent XUSB/XInput-compatible USB slots.

> This is an independent community project. It is not affiliated with, endorsed by, or certified by Microsoft, Nintendo, Raspberry Pi, or 8BitDo. It is a PC adapter, not an Xbox-console accessory.

![Illustrated AUTOFIRE control reference](assets/guide/AUTOFIRE.PNG)

The image is an instructional illustration, not an official product photograph.

## Confirmed hardware and scope

- Raspberry Pi Pico W (RP2040 + CYW43439).
- A USB data cable; charge-only cables do not work.
- Xbox Wireless Controller model 1914, firmware 5.9.2709.0, over BLE.
- 8BitDo Ultimate 2C Bluetooth Controller, Switch profile, over Classic HID.
- Windows 11 using two XUSB/XInput-compatible interfaces.

The user physically validated both controllers operating simultaneously, reconnection and persistent Bluetooth links, lowest-free-slot assignment, independent slot release, the 8BitDo player indicator, per-controller AUTOFIRE, and the read-only USB guide volume. Automated tests do not replace that hardware evidence. No claim is made for two Xbox controllers, PlayStation controllers, more than two players, Xbox consoles, rumble forwarding, or wireless audio.

## Player slots

The two USB outputs remain enumerated. The first controller to reach a valid input-ready state receives the lowest free logical slot. Disconnecting it immediately neutralizes and releases only that slot; the other player is not moved. A reconnecting controller again receives the lowest free slot. Bluetooth identities and keys persist, but player numbers do not.

Windows may assign different global XInput indexes when other controllers are installed. A Pico logical slot is not guaranteed to be global XInput index 0 or 1.

## Install with BOOTSEL

1. Disconnect the Pico W.
2. Hold **BOOTSEL**, connect the USB data cable, and release the button when `RPI-RP2` appears.
3. From PowerShell, explicitly select the desired image:

```powershell
.\Load_Firmware.bat firmware\pico_xinput_dual_autofire_guide_candidate.uf2
```

The loader prints the path and SHA-256 before copying. If the application fails, repeat BOOTSEL; the ROM bootloader remains available.

The development build uses USB identity `VID:PID 2E8A:000A`. PID `000A` is allocated to Raspberry Pi Pico SDK CDC UART and is not assigned to this project. Obtain an appropriate PID before distributing a production binary; see [USB identity and XInput](docs/xinput.md).

## Pairing and reconnection

For Xbox, hold its pairing button until the logo flashes rapidly. For 8BitDo, select Switch Bluetooth mode, turn it on with Home, and hold Pair for approximately three seconds. Pairing is normally required only once. Link data is stored in Pico W flash; ordinary disconnects do not erase it.

The 8BitDo uses Bluetooth Classic HID and the observed `0x3F` report format. The PC sees only the Pico's XInput-compatible USB outputs, not the original Bluetooth profiles.

## AUTOFIRE

Settings are independent per controller and target. They remain in RAM across a Bluetooth reconnection and are cleared when the Pico W restarts. Bluetooth bonds remain persistent.

1. Hold Xbox **View + Menu**, or 8BitDo **Minus + Plus**, for two seconds.
2. Wait for three rapid Pico LED flashes and release both buttons.
3. Press one target: A/B/X/Y, LB/RB (L/R), LT/RT (ZL/ZR), L3, or R3.
4. With no D-pad direction, an unconfigured target is enabled at 10 presses/s and a configured target is disabled.
5. Hold one cardinal direction while selecting: Up 5, Right 10, Down 15, Left 20 presses/s.

The selection is consumed. Diagonals and multiple targets are rejected. Setup times out after ten seconds. AUTOFIRE operates only while the physical target is held. Trigger activation uses 50% with release hysteresis at 45%.

LED feedback: three rapid flashes for setup armed, two short for enabled, one long for disabled, one to four flashes for 5/10/15/20 Hz, and four rapid flashes for cancel/timeout. One shared Pico LED cannot identify which controller requested feedback.

## Read-only `PICO_GUIDE`

The final build adds a static FAT12 USB drive containing `README.TXT`, `MANUAL.HTM`, `AUTOFIRE.PNG`, and `VERSION.TXT`. It is served from immutable flash, advertises write protection, and rejects write/format SCSI commands with DATA PROTECT. It is not the `RPI-RP2` BOOTSEL drive and exposes no Bluetooth keys or firmware storage.

## Reproducible build

Pinned dependencies and checksums are listed in [DEPENDENCIES.md](DEPENDENCIES.md). On Windows:

```powershell
.\setup-deps.ps1
.\test-host.bat
.\build-autofire-guide.bat
```

The final script builds for `pico_w` and writes `firmware\pico_xinput_dual_autofire_guide_candidate.uf2`. Any change to the embedded guide changes the UF2 and SHA-256.

## Diagnostics and validation

- `tools/check-xinput.ps1` lists XInput indexes and changes.
- `tools/measure-autofire.ps1` reports its polling cadence and is fast enough to inspect 20 Hz transitions. It is not an end-to-end latency measurement.
- UART0 GPIO0/GPIO1 at 115200 baud remains an optional 3.3 V diagnostic path.
- [Validation guide](docs/validation.md)
- [Known limitations](docs/limitations.md)
- [Architecture](docs/architecture.md)

The host suite contains 12 tests covering parsers, routing, XInput serialization, reconnection, AUTOFIRE, read-only FAT/MSC, embedded assets, and dual-XUSB-plus-MSC descriptors.

## Licensing and provenance

Project-authored source is under the [MIT License](LICENSE). Dependencies retain their own licences and restrictions. Review [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), [REFERENCES.md](REFERENCES.md), and [LICENSES](LICENSES/) before redistributing binaries. References do not imply copied code, and a URL alone is not a reuse licence.

## Layout

- `src/bluetooth`: BLE and Classic transports and reconnection.
- `src/controllers`: HID and 8BitDo parsers.
- `src/core`: normalized state, routing, and AUTOFIRE.
- `src/usb`: HID, dual XUSB, and read-only guide MSC.
- `src/led`: non-blocking LED arbitration.
- `tests`: host regression tests.
- `assets/guide`: files embedded in `PICO_GUIDE`.
- `docs`: architecture, research, diagnostics, and phase records.
