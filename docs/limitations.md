# Known limitations

- Supported inputs are one Xbox Series 1914 BLE controller and one 8BitDo Ultimate 2C Bluetooth/Switch controller. Two Xbox controllers, two 8BitDo controllers, PlayStation controllers, Xbox 360 receivers, and more than two players are not implemented.
- Output targets Windows XUSB/XInput compatibility. It is not compatible with Xbox consoles and is not Microsoft certified.
- Development USB `VID:PID 2E8A:000A` is not assigned to this project; do not treat the current binary identity as production-ready.
- Rumble forwarding, audio, battery reporting, 8BitDo gyro, Capture, advanced rumble, and proven independent L4/R4 are not implemented.
- 8BitDo ZL/ZR are digital in the observed profile and map to 0/255.
- AUTOFIRE settings live in RAM and disappear on Pico restart. Bluetooth links remain persistent.
- The shared LED cannot identify which controller generated simultaneous feedback.
- The two fixed USB slots may remain visible and neutral when no Bluetooth controller owns them.
- No measured end-to-end latency or endurance guarantee is published.
