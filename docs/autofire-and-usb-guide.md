# Per-controller AUTOFIRE and read-only USB guide

## Scope

This candidate extends the validated dual-controller bridge with two features:

- Per-controller, per-button AUTOFIRE held entirely in RAM.
- A static, read-only USB mass-storage volume named `PICO_GUIDE`.

It retains the two fixed XUSB interfaces, Xbox Series 1914 BLE input, 8BitDo
Ultimate 2C Classic HID input, persistent Bluetooth links, transient lowest-free
player slots, independent neutralisation and the 8BitDo player indicator.

The previous validated firmware remains at
`firmware/pico_xinput_dual_release_slots_led_candidate.uf2`.

## AUTOFIRE architecture

The pipeline is deliberately one way:

`physical state -> command detector -> AUTOFIRE engine -> transformed state -> XUSB slot`

The transformed state is never fed back to the command detector. Each supported
controller identity has its own RAM settings, setup state and phase state. Settings
survive a Bluetooth disconnect/reconnect of that controller but are erased when the
Pico W restarts. Bluetooth keys remain persistent as before.

Enter setup by holding View+Menu on Xbox or Minus+Plus on 8BitDo for two seconds.
Release both buttons, then select A/B/X/Y, shoulders, triggers or a stick click.
Selecting without the D-pad toggles 10 Hz. A cardinal direction selects 5 Hz
(up), 10 Hz (right), 15 Hz (down) or 20 Hz (left). The embedded manual contains
the complete interaction and LED reference.

The engine uses monotonic microseconds and derives phase from elapsed time rather
than accumulating rounded periods. It therefore does not build a backlog after a
delayed tick and does not round 15 Hz into a drifting integer period. Configured
triggers use 50% press and 45% release thresholds.

## USB layout and resource audit

The final USB configuration contains three interfaces:

1. XUSB player slot 1, interrupt IN/OUT endpoints `0x81`/`0x02`.
2. XUSB player slot 2, interrupt IN/OUT endpoints `0x83`/`0x04`.
3. MSC guide, bulk OUT/IN endpoints `0x06`/`0x85`.

The Microsoft OS compatibility descriptor still identifies only the two XUSB
interfaces. The MSC interface uses the standard Windows mass-storage class.

The synthetic FAT12 image is generated sector by sector from immutable flash data;
the complete disk is never copied into RAM. TinyUSB advertises the medium as not
writable. WRITE and FORMAT/MODE SELECT paths fail with SCSI DATA PROTECT rather
than reporting false success. The volume does not expose the Bluetooth TLV store,
firmware or keys.

Final linked resources for this candidate:

- Flash image range: `0x10000000` through `0x10110844` (1,116,228 bytes).
- Runtime `.data`: 7,172 bytes.
- Runtime `.bss`: 55,700 bytes.
- Configured heap: 2,048 bytes.
- Configured stack reservation reported by the linker: 2,048 bytes.
- The BTstack flash-bank area remains at the end of the 2 MiB flash and does not
  overlap the firmware or embedded documents.

## Automated verification

Run:

```powershell
.\test-host.bat
.\build-autofire-guide.bat
```

The host suite covers the existing parsers/routes plus AUTOFIRE timing, setup,
trigger hysteresis, reconnect gating, two independent engines, FAT reads and
bounds, write protection, embedded assets, and the dual-XUSB-plus-MSC descriptors.

## Loading

Put the Pico W in BOOTSEL and run this explicit command. The loader prints the
selected path and SHA-256 before copying:

```powershell
.\Load_Firmware.bat firmware\pico_xinput_dual_autofire_guide_candidate.uf2
```

## Physical validation still required

Software build and host tests do not validate Windows enumeration or controller
behaviour. On Windows 11 verify:

1. Both Pico XInput outputs are visible and independent.
2. `PICO_GUIDE` appears, all four files open and can be copied, and creating a
   file on it fails as write-protected. Do not format the disk.
3. Configure AUTOFIRE on one controller while the other continues playing.
4. Hold each configured target at 5/10/15/20 Hz and observe it with
   `tools/measure-autofire.ps1`; use the default high-rate polling and heed its
   missed-transition warning.
5. Copy `AUTOFIRE.PNG` repeatedly while both controllers are active.
6. Disconnect and reconnect each controller independently. Its USB slot must
   neutralise, no stale ON phase may return, and the other controller must remain
   active.
7. Restart the Pico: Bluetooth links must remain, while AUTOFIRE settings must be
   cleared.

The Windows polling tool measures observed XInput transitions, not physical
end-to-end latency.
