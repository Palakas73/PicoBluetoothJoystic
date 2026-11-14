# XUSB/XInput-compatible output

The current backend implements the wired Xbox 360-style XUSB interface described by MS-XUSBI, not Xbox One GIP. Each of the two interfaces has an XUSB class-specific descriptor, interrupt IN/OUT endpoints, a 20-byte input report, and a Microsoft OS 1.0 `XUSB10` compatible ID.

Mapped controls are D-pad, Start/Menu, Back/View, L3/R3, LB/RB, A/B/X/Y, two 8-bit triggers, and two signed 16-bit sticks. Share/Capture has no standard field in this report. Guide support and rumble forwarding are not promised. OUT reports are accepted/accounted for but controller rumble routing is not complete.

## USB identity warning

The development build uses `VID:PID 2E8A:000A`. Raspberry Pi's registry assigns PID `000A` to Pico SDK CDC UART (RP2040), not this project. It was retained during private validation to avoid changing the already-working Windows binding, but it is not a project-owned production identity. Apply for an appropriate Raspberry Pi PID or use another authorized VID/PID, then retest Windows binding before distributing firmware.

Changing VID/PID alone does not implement XInput. Windows must also accept the descriptors, compatible IDs, endpoints, and host initialization behavior. No Microsoft VID/PID is used, and no affiliation or certification is claimed.

## Validation status

The user physically validated two simultaneous XInput outputs with the supported Xbox and 8BitDo controllers. Automated tests cover report serialization, neutral priority, latest-state replacement, descriptors, requests, and independent routing. Compatibility with every Windows version/game and physical end-to-end latency are not claimed.

See [Microsoft MS-XUSBI](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-xusbi/) and [Raspberry Pi USB PID allocations](https://github.com/raspberrypi/usb-pid).
