# DualShock 4 Classic HID candidate (stage 1)

## Scope and status

This stage adds a DualShock 4-compatible Bluetooth Classic HID profile to the existing single Classic HID host. The target pair is Xbox Series 1914 over BLE plus one DS4 over Classic HID, using the existing two XInput USB outputs and PICO_GUIDE. DS4 parsing has automated fixtures. The user reports that DS4 controls and AUTOFIRE work in The Occultist; exact hardware revision and Xbox + DS4 simultaneous operation were not established. Small stick movement remains visible in `joy.cpl`, and raw Bluetooth reports have not yet been captured. Treat the DS4 profile as experimental.

The radio has one BLE controller path and one active Classic HID connection. Thus Xbox + DS4 is the intended simultaneous pair, while DS4 + 8BitDo, DS4 + Switch Pro, 8BitDo + Switch Pro and two DS4 controllers are not simultaneous configurations in this stage. Two XInput USB outputs do not create a second Classic radio connection. The unused output stays neutral.

## Profile selection and input formats

The Bluetooth name is only a discovery hint. The DS4 parser is selected after checking the received HID report descriptor for the Sony vendor page plus expected report IDs. It then accepts only Bluetooth input report `0x01` at exactly 10 bytes or `0x11` at exactly 78 bytes. The HIDP `A1` input prefix is removed once by the Classic host before parsing. The 0x11 CRC is little-endian CRC32 over the report ID and bytes before the final CRC, with seed byte `A1`, reflected IEEE polynomial `0xEDB88320`, and final complement.

The current host does not query the Classic Bluetooth Device ID (PnP) SDP service, so it cannot independently verify the remote vendor/product IDs `054C:05C4` or `054C:09CC`. Descriptor and report validation confirms a DS4-compatible report profile, not that a device is an original Sony unit. Clone support is not claimed; this identity check remains a limitation for the physical validation pass.

Report `0x01` contains axes at offsets 1–4, buttons/hat at 5–7, and triggers at 8–9. Report `0x11` begins with two reserved bytes after the ID; axes are at 3–6, buttons/hat at 7–9, and analog triggers at 10–11. The descriptor must be confirmed before this parser is selected. INPUT_READY is reported only after a correctly sized valid controls report (and a valid CRC for `0x11`).

At descriptor confirmation the firmware sends an asynchronous Feature Report `0x02` request based on Bluepad32's observed stream-mode initialization. This request is optional: it has a timeout, and controls from a valid 0x01 report do not wait for it. Linux documents a separate Bluetooth sensor calibration feature report `0x05`; sensor calibration is outside this stage. No Nintendo initialization command is sent to a descriptor-confirmed DS4. Lightbar output is not implemented.

## Controls and AUTOFIRE

Physical positions map as follows: Cross → Xbox A, Circle → B, Square → X, Triangle → Y; L1/R1 → LB/RB; analog L2/R2 → LT/RT; SHARE/OPTIONS → View/Menu; L3/R3 and D-pad map directly. PS and touchpad click are not mapped. Motion, touch gestures, audio, rumble, battery data and lightbar color are outside scope.

AUTOFIRE setup uses SHARE + OPTIONS for two seconds. Settings are held in a bounded RAM registry keyed by stable Bluetooth Classic address, survive reconnecting the same controller during the Pico session, do not transfer when another Classic controller reuses the host context, and clear on Pico reboot. Timing phases remain connection-local and are cancelled on disconnect.

## Build and load

The scripts use the project's pinned Pico SDK/toolchain and target `pico_w`:

```powershell
.\build-dualshock4-xinput.bat
.\build-dualshock4-diagnostic.bat
```

To load the candidate XInput build (two XUSB outputs plus PICO_GUIDE):

```powershell
.\Load_Firmware.bat firmware\pico_xinput_dual_ds4_candidate.uf2
```

To load the CDC diagnostic build instead:

```powershell
.\Load_Firmware.bat firmware\pico_ds4_cdc_diagnostic.uf2
```

The diagnostic firmware exposes CDC and a generic HID gamepad for capture. It is not the dual XInput descriptor build and cannot validate Windows XUSB association. The XInput candidate has no USB CDC serial interface; its diagnostic output remains on the optional UART pins.

## Guided physical capture

1. Load `pico_ds4_cdc_diagnostic.uf2` by holding BOOTSEL during USB connection.
2. Find the Pico CDC port with `[System.IO.Ports.SerialPort]::GetPortNames()`.
3. Turn the DS4 off. Hold SHARE, then press/hold PS until the light bar flashes.
4. Run `.\tools\capture-dualshock4.ps1 -Port COMx`. It records diagnostics and does not start control prompts until `[DS4-INPUT] INPUT_READY` appears.
5. Follow its prompts. Hold each named physical control for about two seconds and return to rest between actions. The script writes the log while showing output.
6. If the controller never reaches INPUT_READY, stop the capture with Ctrl+C; the log is flushed as it is received and is retained at the displayed output path.

For a hands-off rest capture, leave the DS4 flat and untouched after it reaches INPUT_READY, then run:

```powershell
.\tools\capture-dualshock4.ps1 -Port COMx -RestOnly -RestSamples 24
```

The script sends `DS4 CAPTURE 24` over CDC. The Pico samples at most once per 100 ms into a bounded queue and prints the complete incoming report plus the same sample's report ID/framing, raw stick bytes, parser result, CRC (for `0x11`), normalized `ControllerState`, and calculated serialized XInput axes. The report bytes are copied into Pico-owned storage inside the callback; CDC output happens later from the timer, not from the Bluetooth callback. `omitted` is included in the completion line. A sample shows whether BTstack supplied HIDP prefix `A1`; that prefix is removed once before parsing.

The parser expects Bluetooth report `0x01` at 10 bytes (axes at payload offsets 1–4) or `0x11` at 78 bytes (axes at offsets 3–6). Both encode each axis as an unsigned byte with 128 mapped to center; normalization is deterministic and does not estimate a center. The final `XInput` values in the trace are produced by the same report encoder as the backend, including the backend's Y-axis inversion. Use the correlated raw values to decide whether rest jitter is already present in the controller reports or introduced later. This CDC image calculates XInput values for diagnosis but exposes generic HID, not the production XUSB interfaces.

The log distinguishes name hint, descriptor confirmation, feature request/response/timeout, report IDs and lengths, parser rejection counts (including CRC), valid input readiness, logical slot and normalized state. It does not print link keys.

Classic identity storage holds four remembered controllers. At full capacity use the diagnostic CDC console's `BOND PAIR` command before entering Pair mode. The firmware stages the new link key in BTstack's fifth key slot and only replaces the least recently used disconnected identity after DS4/profile validation and a valid input report. A failed pairing leaves the previous identity and key intact. `BOND REMOVE` accepts an exact address for selective removal.

## Validation after capture

The user reports testing the DS4 candidate in The Occultist with controls and AUTOFIRE working. The supplied Windows XInput capture shows small axis movement in `joy.cpl`, but does not contain raw Bluetooth reports, so it cannot establish whether this movement originates in the controller or a later stage. Xbox BLE + DS4 simultaneous operation has not been reported as tested. The exact UF2 used for the reported game test was `pico_xinput_dual_ds4_candidate.uf2`, SHA-256 `5BDD22ACC559FEFEF466F01C7C1B47990824449906CD2FEEC70E27207292C580`; see [hardware validation record](hardware-validation-2026-11-14.md).

## References and code provenance

The parser is project-authored and does not copy or translate source. Report layout is cross-checked against Bluepad32's Apache-2.0 `uni_hid_parser_ds4.c` and Linux's GPL-2.0-or-later `hid-playstation.c`; pairing instructions come from Sony support. See [REFERENCES.md](../REFERENCES.md) and [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md). Linux and Bluepad32 are references only, not linked dependencies.
