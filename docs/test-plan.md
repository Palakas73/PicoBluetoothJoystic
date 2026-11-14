# Test plan

## Automated host tests

Run `test-host.bat`. The suite covers Xbox HID parsing, HID output, reconnect policy, XInput serialization, 8BitDo `0x3F` parsing, dual routing/generation protection, player lights, AUTOFIRE timing and isolation, FAT reads/bounds, SCSI write protection, embedded guide assets, and dual-XUSB-plus-MSC descriptors.

AUTOFIRE tests use a simulated monotonic clock for the 1999/2000 ms boundary, one-shot arming, mandatory release, four rates, exact 15 Hz phase without accumulated rounding, timeout/cancel, trigger hysteresis, reconnect gating, and two independent controllers.

## Physical regression

1. Test Xbox alone and 8BitDo alone.
2. Test both connection orders and simultaneous independent controls.
3. Disconnect one while holding a control on the other; only the disconnected slot must neutralize.
4. Reconnect both, repeat five cycles each, then power-cycle Pico and verify both links.
5. Validate AUTOFIRE targets/rates independently and confirm settings clear only after Pico restart.
6. Open/copy all `PICO_GUIDE` files and verify file creation fails as write-protected. Do not format it.
7. Repeat guide-file reads while both controllers are active.

Host tests and compilation do not replace Windows and hardware validation. Windows polling is not end-to-end latency measurement.
