# 8BitDo Ultimate 2C Bluetooth

The supported model is the Bluetooth/Switch edition. It connects to Pico W as a Bluetooth Classic HID device; it is not used in XInput mode over Bluetooth. The PC sees the Pico's XUSB output.

## Pairing

1. Start the Pico firmware.
2. Select the controller's Switch Bluetooth mode and turn it on with Home.
3. Hold Pair for about three seconds until pairing mode starts.
4. The Pico performs inquiry, SDP HID discovery, authentication, and opens HID control/interrupt channels.

After the first successful bond, normal power-on should reconnect without Pair whether the Pico or controller starts first. Link keys persist across Pico power loss. Ordinary disconnects do not erase them.

## Mapping

Physical positions are preserved for Xbox output: lower B -> XInput A, right A -> B, left Y -> X, upper X -> Y; Minus/Plus -> Back/Start; L/R -> LB/RB; ZL/ZR -> digital 0/255 triggers; stick clicks and D-pad map directly. Home/Guide, Capture, gyro, advanced rumble, and independent L4/R4 are not guaranteed.

The observed controller sends `0x3F` input reports rather than continuing in the requested Switch Pro `0x30` mode. See [0x3F findings](8bitdo-3f-findings.md).
