# Nintendo Switch Pro Controller

The user subsequently confirmed the Switch Pro as a functioning controller alternative. This record retains the original diagnostic history; it does not claim simultaneous operation with another Bluetooth Classic controller.

## Root cause in the previous firmware

The `switch-pro-20261109-211112.log` capture proves that the failure occurred before Switch initialization: every outgoing attempt targeted the remembered 8BitDo address, while `descriptor_len=0` and `rx=0`. No parser offset can fix that condition.

The concrete state-machine defect was the timeout path. It called `hid_host_disconnect()` while the outgoing HID host operation was still waiting for SDP/ACL creation. BTstack's HID host has no control or interrupt channel at that point, so that function performs no cancellation. The timer then fired again for the same CID and the host never reached inquiry. `gap_connect_cancel()` is not a valid replacement in the pinned BTstack version because its implementation only handles LE connection establishment.

The corrected host submits the Classic `HCI_Create_Connection_Cancel` command once, keeps the CID and attempt generation reserved, and waits for BTstack to propagate the command completion through L2CAP to the final HID-open failure. Only after that final event does it clear the CID and request a real inquiry. A late event whose CID/generation does not identify the current attempt is ignored. This preserves the 8BitDo link key and does not require bond reset.

The candidate stores four committed Classic identities. BTstack has five Classic key records, reserving one provisional slot for transactional replacement. The v2 identity list is migrated to v3 while retaining its existing BTstack key tags. The list is ordered least-recently-used to most-recently-used and updates only when a controller reaches `INPUT_READY`; directed reconnect rotates fairly across the stored identities. Diagnostics report remembered, key, active Classic, and USB slot counts separately. Names remain candidate evidence because some 8BitDo units also advertise `Pro Controller`.

## Protocol

After HID control/interrupt channels and descriptor availability, the host sends output report `0x01` with neutral rumble and subcommand `0x02` (device information). It does not log the returned controller address. It then sends subcommand `0x03` with value `0x30` and requires a matching ACK. HIDP `A1` is removed once.

- `0x21`: initialization/subcommand reply; never routed as controls.
- `0x30`: full Switch report with three button bytes and packed 12-bit sticks; required to mark an ambiguous `Pro Controller` as input-ready.
- `0x3F`: separate simple format used by the physically observed 8BitDo path; it is parsed by a different function and never with `0x30` offsets.

Stick calibration is currently centered/scaled from the nominal 12-bit range. Factory/user SPI calibration retrieval is not yet implemented, so physical center and extremes must be measured. Gyroscope, NFC and advanced rumble are outside this phase.

## Pairing test

1. Turn off the 8BitDo for the first Switch Pro test.
2. Load `firmware/pico_xinput_dual_switch_pro_candidate.uf2` through BOOTSEL.
3. Hold the Switch Pro **SYNC** button until its player LEDs chase.
4. Wait for pairing. Do not assume that pressing Home alone starts first-time pairing.
5. Test buttons, D-pad, L/R, ZL/ZR, stick clicks and both sticks with `tools/check-xinput.ps1`.

If it does not become responsive, load `firmware/pico_switch_pro_cdc_diagnostic.uf2`, identify its USB COM port, and run `powershell -ExecutionPolicy Bypass -File tools\capture-switch-pro.ps1 -Port COMx`. The tool waits up to 75 seconds for an interpreted control report. It starts the guided button sequence only after `INPUT_READY`; otherwise it exits with connection diagnostics and preserves the log. Retain lines beginning `[BT-CLASSIC]`, `[SWITCH-INIT]`, `[SWITCH-IDENTITY]`, `[SWITCH-DIAG]`, `[SWITCH-DESCRIPTOR]`, and `[SWITCH-RAW]`.

Expected state-machine evidence for the previously failing case is: `directed requested`, one `directed timeout`, one `cancel submitted`, `cancel completed`, final `HID open failed`, `inquiry requested ... active=1`, one or more `inquiry result`, and `inquiry completed` or a candidate connection. A status variable saying `scanning` without those inquiry events is not treated as proof of discovery.

## Normal reconnection test

A short press of **SYNC** disconnects the Switch Pro; it is not the normal command for entering pairing unless held. The firmware retains the identity and Classic link key. After a connected Classic controller disconnects, the Pico keeps page scan enabled and opens a 12-second quiet incoming-reconnection window. Pressing **A** during this window allows the bonded controller to initiate HID again without competing with an outgoing page to another remembered controller. If no incoming connection occurs, the disconnected identity receives the first directed attempt, after which remembered Classic identities rotate fairly. A controller that remains connected is never displaced. At full four-identity capacity, adding another Classic controller requires explicit `BOND PAIR` authorization in the CDC diagnostic firmware and commits an LRU replacement only after valid input readiness.

The CDC diagnostic logs the HCI disconnect reason, target identity, whether a link key exists, page-scan window, each outgoing target, incoming HID decisions, authentication, encryption, and HID recovery. Link-key bytes are never printed.

Run the dedicated test without Pair mode:

```powershell
powershell -ExecutionPolicy Bypass -File tools\capture-switch-pro-reconnect.ps1 -Port COMx
```

Start with the Switch Pro already connected and responsive. When instructed, press **SYNC briefly**, wait for the next instruction (ten seconds), then press **A** once. The tool does not start the general button sequence and records whether input-ready returns without Pair.

Xbox + Switch Pro can use the separate BLE and Classic transports. The current Classic transport implementation still opens one HID connection at a time. Therefore 8BitDo + Switch Pro simultaneous operation is not supported.
