# Reconnection

Xbox BLE bonding and the 8BitDo Classic link key are persisted in BTstack's flash store. Normal power-off, radio loss, or controller disconnect does not delete them. Flash is not written per report or per retry.

The Classic host remembers four identities. BTstack has five Classic key slots so a new bond can be staged transactionally while preserving all four committed keys. At full identity capacity, a new controller is admitted for replacement only after explicit `BOND PAIR` authorization through the CDC diagnostic firmware. The least recently used disconnected and unreserved identity is selected, but its key and RAM AUTOFIRE context are kept until the new controller has been identified and delivered a valid controls report. A failed or interrupted pre-commit attempt removes only the provisional new key; a validated commit interrupted by power loss is completed on boot. `BOND REMOVE AA:BB:CC:DD:EE:FF` selectively removes one disconnected Classic bond; it does not erase Xbox BLE data.

Xbox uses its BLE identity/bond rather than a transient private address. 8BitDo normal reconnection supports both an incoming HID link and a direct connection to the remembered Classic address; inquiry/name matching is used for new pairing, not as the only reconnect mechanism.

Disconnecting either controller immediately neutralizes only its output and clears live AUTOFIRE phase. Configured AUTOFIRE targets remain associated with that controller in RAM but must be observed released after reconnect before firing again.

The user physically validated reconnection of both supported controllers and persistence across Pico power cycling. No claim is made for arbitrary third controllers or two devices of the same supported model.
