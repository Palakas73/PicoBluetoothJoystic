# Player order and slots

Persistent storage retains controller identities and Bluetooth keys, not player numbers. At boot both logical slots are free. A controller receives the lowest free slot only after initialization has completed and a valid control report has been parsed (`INPUT_READY`). A partial Bluetooth link is insufficient.

On disconnect, the generation-matched owner is neutralized and its slot is immediately released. The other controller is never moved. A reconnect receives the current lowest free slot. Late callbacks from an older connection generation cannot neutralize or release a slot already reused by another context.

Windows global XInput indexes can differ from Pico logical slots when other controllers are present.
