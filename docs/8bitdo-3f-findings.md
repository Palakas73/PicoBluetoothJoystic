# 8BitDo `0x3F` report findings

The physical capture `8bitdo-20261108-123429.log` established sustained Classic HID traffic on CID 1. BTstack delivered 13-byte frames commonly beginning `A1 3F`. `A1` is the HIDP DATA input header; it must be removed exactly once. The parser then receives Report ID `0x3F` and eleven payload bytes.

Observed layout: byte 0 ID, bytes 1-2 buttons, byte 3 hat (`8` neutral), and bytes 4-11 containing four little-endian 16-bit axes. Neutral sample `3F000008008000800080F07F` supports centers near `0x8000`. Real controller captures, not an assumed original Switch Pro `0x30` layout, are the basis of the parser and regression fixtures.

An initialization response `0x21` carried ACK `0x80` for subcommand `0x03`, but later controls remained `0x3F`; ACK is therefore not treated as proof of `0x30` mode. Initialization replies never enter the control parser.

Physical LY evidence was `-32767` up, `32752` down, and `0` at rest. The normalized parser retains this orientation and XUSB performs the single required Y conversion using widened, saturated arithmetic. RY was not changed without equivalent identified evidence.
