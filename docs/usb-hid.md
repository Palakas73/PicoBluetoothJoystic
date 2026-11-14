# Legacy USB HID backend

The preserved HID backend exposes a standard gamepad with independent 16-bit sticks, analog triggers, D-pad, and buttons. It publishes neutral on startup/disconnect and retains only the latest pending state when the endpoint is busy.

USB HID is not XInput. The current dual candidate does not reintroduce generic HID gamepad interfaces; it uses two XUSB-compatible outputs. Earlier HID UF2 files remain historical recovery artifacts in the private development repository.

The parser's Bluetooth report map and the USB HID output descriptor are independent. Axis labels displayed by `joy.cpl` can vary and require physical verification.
