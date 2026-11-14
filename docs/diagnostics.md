# Diagnostics

Normal builds keep hot-path logging bounded and use UART0 GPIO0/GPIO1 at 115200 baud as an optional 3.3 V diagnostic channel. Do not connect 5 V logic. Temporary CDC builds exist for captures when no UART adapter is available.

Diagnostics distinguish discovery, link/authentication, HID/HOGP initialization, subscription, raw report reception, parser acceptance/rejection, normalized state, logical slot, USB queue/completion, disconnect, and retry. They never print Bluetooth keys.

Use `tools/check-xinput.ps1` to enumerate Windows XInput indexes and `tools/measure-autofire.ps1` for high-rate transition observation. `joy.cpl`, a fixed LED, or a USB product string alone is not proof of XInput routing or parsed input.
