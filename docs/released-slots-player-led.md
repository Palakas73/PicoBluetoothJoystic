# Released slots and 8BitDo player LED

The validated policy assigns only active, input-ready controllers. Disconnecting Xbox from slot 1 while 8BitDo owns slot 2 leaves 8BitDo uninterrupted in slot 2; the next ready controller receives slot 1. If both disconnect, both slots become free.

The 8BitDo player-light command is sent after logical-slot assignment: slot 1 requests indicator 1 and slot 2 requests indicator 2. The firmware records the subcommand response but does not infer full Switch Pro compatibility from that response.

USB outputs remain enumerated and neutral while slots are free. USB is not restarted during Bluetooth ownership changes.
