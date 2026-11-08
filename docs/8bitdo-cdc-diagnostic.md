# Temporary 8BitDo CDC diagnostic firmware

`build-8bitdo-diagnostic.bat` creates `firmware/pico_8bitdo_cdc_diagnostic.uf2`. It preserves the Classic Bluetooth path but uses CDC plus generic HID for evidence collection; it does not validate XInput or two-player behavior.

Load it explicitly with `Load_Firmware.bat firmware\pico_8bitdo_cdc_diagnostic.uf2`, locate the new COM port, then run:

```powershell
powershell -ExecutionPolicy Bypass -File tools\capture-8bitdo.ps1 -Port COMx
```

The capture records pre-parser reports with monotonic timestamp, ID, full delivered bytes and length, including rejected reports. Its bounded buffer records omitted/oversize counts and drains outside the Bluetooth callback. Follow the guided idle/A/B/X/Y/D-pad/shoulder/trigger/stick sequence and hold each action long enough to avoid missing it.

Connection, initialization, raw reception, parser acceptance, normalized state, and USB publication are separate milestones. A fixed controller LED proves only a link state.
