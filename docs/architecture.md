# Architecture

The firmware is an event-driven bridge with two independent input contexts and two fixed USB output slots.

`Bluetooth transport -> controller parser -> ControllerState -> command detector -> AUTOFIRE -> transformed state -> XUSB slot`

- `bluetooth/`: Xbox BLE HOGP and a single Classic HID transport, pairing, keys, reconnect policy, and connection generation.
- `controllers/`: Classic profile identification, Switch-compatible parsers, and transport-independent DS4 parsing into normalized physical state.
- `core/`: connection generations, lowest-free slot routing, independent neutralization, and per-identity RAM AUTOFIRE.
- `usb/`: two XUSB-compatible interfaces and an immutable FAT12 MSC guide.
- `led/`: one non-blocking priority arbiter for critical errors, setup feedback, and connection state.

Each controller context owns its handle, transport, parser state, counters, generation, physical state, transformed state, and current slot. Late callbacks are rejected using the connection generation. Disconnecting one context immediately publishes neutral to and releases only its slot.

The radio architecture has one BLE controller path and one active Classic HID path. The Classic transport selects a profile after descriptor inspection; the DS4 parser additionally validates the Bluetooth report ID, exact length, and (for report `0x11`) CRC. The Classic host is still a single active connection, so it cannot host a DS4 and an 8BitDo/Switch Pro at the same time. Xbox BLE + one Classic device is the intended dual-transport configuration. The user reports DS4 controls and AUTOFIRE working alone in The Occultist; simultaneous Xbox BLE + DS4 operation remains unvalidated.

Bluetooth identities and keys are persistent. Slot assignments and AUTOFIRE phases are runtime state. AUTOFIRE rates are stored in a bounded RAM registry keyed by Classic Bluetooth address, loaded when that peer is identified, and cleared on Pico reboot. Connection-local phases are reset on disconnect. The USB interfaces remain enumerated even when neutral and unassigned.

Classic persistence separates four remembered identities from five BTstack key records: the fifth record is provisional space for transactional replacement. Existing v2 identity records are migrated to v3 without changing the existing `BTL0`/`BTL1` key tags. The Classic identity list is authoritative after migration, and a startup recovery marker resolves power loss: unvalidated provisional keys are removed, while a transaction whose valid `INPUT_READY` commit intent was persisted is completed. LRU order is updated and stored only on first `INPUT_READY` per connection. The 8 KiB Pico SDK BTstack flash bank layout is unchanged; the additional key/list/transaction records fit in that existing TLV area.
