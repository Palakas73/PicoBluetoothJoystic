# Architecture

The firmware is an event-driven bridge with two independent input contexts and two fixed USB output slots.

`Bluetooth transport -> controller parser -> ControllerState -> command detector -> AUTOFIRE -> transformed state -> XUSB slot`

- `bluetooth/`: Xbox BLE HOGP, 8BitDo Classic HID, pairing, keys, reconnect policy.
- `controllers/`: transport-independent parsing into normalized physical state.
- `core/`: connection generations, lowest-free slot routing, independent neutralization, and per-identity RAM AUTOFIRE.
- `usb/`: two XUSB-compatible interfaces and an immutable FAT12 MSC guide.
- `led/`: one non-blocking priority arbiter for critical errors, setup feedback, and connection state.

Each controller context owns its handle, transport, parser state, counters, generation, physical state, transformed state, and current slot. Late callbacks are rejected using the connection generation. Disconnecting one context immediately publishes neutral to and releases only its slot.

Bluetooth identities and keys are persistent. Slot assignments and AUTOFIRE settings are runtime state. The USB interfaces remain enumerated even when neutral and unassigned.
