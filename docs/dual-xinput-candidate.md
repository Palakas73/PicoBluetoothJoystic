# Dual XUSB/XInput candidate

The device exposes two fixed vendor-specific XUSB interfaces. Slot 1 uses endpoints `0x81/0x02`; slot 2 uses `0x83/0x04`. The Microsoft OS 1.0 compatible-ID response contains two `XUSB10` functions. A third standard MSC interface serves `PICO_GUIDE` on `0x85/0x06`.

Windows may keep both Pico gamepad outputs connected while an unused slot is neutral. Bluetooth connect/disconnect events never re-enumerate USB. Each slot sends only the latest state and does not accumulate stale reports when an endpoint is busy.

The user physically validated simultaneous independent input. This does not certify compatibility with every Windows build or game. The current development VID/PID is not assigned to this project and must be replaced before production distribution.
