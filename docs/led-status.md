# LED behavior

The Pico W's green LED is controlled by one non-blocking arbiter. Priority is critical error, configuration feedback, then normal connection state. A bounded feedback queue prevents one controller from blocking the other.

- Startup: two flashes.
- Scanning: slow blink.
- Pairing/connecting: fast blink.
- At least the intended operational state: steady according to the current connection pattern.
- Recoverable error: three flashes followed by retry.
- AUTOFIRE armed: three rapid flashes.
- Enabled/updated: speed/count feedback; disabled: one long flash; cancel/timeout: four rapid flashes.

The 8BitDo player indicators are controlled separately and follow its current logical slot. A single Pico LED is not evidence that both inputs and both USB outputs work.
