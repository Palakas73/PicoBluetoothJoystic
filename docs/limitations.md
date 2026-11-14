# Known limitations

- There is one BLE input path (Xbox Series 1914) and one active Bluetooth Classic HID connection (8BitDo Ultimate 2C, Switch Pro, or experimental DualShock 4). The user confirmed Xbox, 8BitDo and Switch Pro work as alternatives, and reports DS4 controls and AUTOFIRE working alone in The Occultist. Xbox + DS4 simultaneous operation remains pending physical validation.
- The Classic host supports only one active Classic controller. 8BitDo + Switch Pro, DS4 + 8BitDo, DS4 + Switch Pro, and two DS4 controllers cannot operate simultaneously. The second USB XInput slot does not increase Classic Bluetooth capacity.
- Four Classic identities are remembered. BTstack uses five Classic link-key records so one candidate key can be staged without evicting an existing bond; only four are committed identities. When full, an explicit `BOND PAIR` command in the CDC diagnostic build authorizes a transactional replacement of the least recently used disconnected, unreserved identity. The prior key is retained until a supported profile reaches `INPUT_READY`.
- DS4 parsing is a candidate tested with synthetic host fixtures only. Pairing, original-unit identification, report mode transitions, axes/buttons, reconnect, and Xbox BLE + DS4 operation require a physical DS4 test.
- Two Xbox controllers, Xbox 360 receivers, and more than two players are not implemented.
- Output targets Windows XUSB/XInput compatibility. It is not compatible with Xbox consoles and is not Microsoft certified.
- Development USB `VID:PID 2E8A:000A` is not assigned to this project; do not treat the current binary identity as production-ready.
- Rumble forwarding, audio, battery reporting, 8BitDo gyro, Capture, advanced rumble, and proven independent L4/R4 are not implemented.
- 8BitDo ZL/ZR are digital in the observed profile and map to 0/255.
- AUTOFIRE settings live in RAM and disappear on Pico restart. Bluetooth links remain persistent.
- The shared LED cannot identify which controller generated simultaneous feedback.
- The two fixed USB slots may remain visible and neutral when no Bluetooth controller owns them.
- No measured end-to-end latency or endurance guarantee is published.
