# Protocol and implementation research

## Xbox model families

Xbox One controller revisions 1537 and 1697 use proprietary Xbox Wireless and do not provide the same Bluetooth path. Model 1708 introduced Bluetooth Classic-era interoperability, while Series model 1914 exposes BLE HID/HOGP in the configuration tested here. Firmware revisions can change report maps and behavior, so model number alone is not a parser contract.

The supported device is model 1914 with firmware 5.9.2709.0. The firmware discovers HID service `0x1812`, reads the report map and references, establishes encryption/bonding, enables notifications, and parses reports into `ControllerState`. The user's physical validation is specific to that hardware/firmware combination.

## 8BitDo

The Ultimate 2C Bluetooth edition presents a Switch-compatible Bluetooth Classic HID profile. Initial research used public Switch `0x30` reports and subcommands, but the actual device capture repeatedly delivered HIDP `A1`, Report ID `0x3F`, length 13. The implementation now accepts the observed format and keeps initialization replies separate from control reports.

## USB output

Standard USB HID and XInput/XUSB are different. The stable earlier HID firmware works with generic HID APIs; the current dual firmware uses two vendor-specific XUSB-compatible interfaces, `XUSB10` OS compatible IDs, 20-byte input reports, and independent IN/OUT endpoints. The current project does not implement Xbox One GIP.

The read-only guide adds a standard MSC interface backed by a synthetic FAT12 image. The full image is not copied to RAM; sectors are generated from immutable flash. Write and format commands return SCSI DATA PROTECT.

## Source-use conclusions

Pico SDK, BTstack, TinyUSB, and CYW43 are build/runtime dependencies. Joypad OS, GP2040-CE, Buddycon/OGX-Mini, Linux `hid-nintendo`, Nintendo Switch notes, and Microsoft specifications were consulted for architecture or protocol comparison. The repository audit found no copied source from those reference-only projects. See [../REFERENCES.md](../REFERENCES.md) and [../THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) for commits, licences, and provenance cautions.
