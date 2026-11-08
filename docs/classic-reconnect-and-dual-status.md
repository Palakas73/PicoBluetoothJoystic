# Classic reconnection and dual status

Classic inquiry discovers only discoverable devices, so a bonded 8BitDo cannot rely on name discovery for normal reconnection. The firmware retains identity/link keys, accepts incoming HID connections, and also retries a direct outgoing HID connection to the known address. Failed attempts release HID resources and return to a recoverable scan/retry schedule.

The Xbox BLE context and 8BitDo Classic context have independent handles, parsers, counters, reconnect policies, states, and USB ownership. Statistics label the transport explicitly; an Xbox `link=0` line does not describe an active Classic connection.

The user physically validated each controller alone, then both simultaneously. Turning one off neutralizes only its XUSB slot while the other continues.
