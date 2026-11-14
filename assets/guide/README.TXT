PicoBluetoothJoystic - Quick Guide
==================================

SUPPORTED HARDWARE AND BLUETOOTH LIMITS
The Pico W has two fixed XInput USB outputs, one BLE/HOGP controller path,
and one Bluetooth Classic HID path. Only ONE Classic controller can connect
at a time. Use up to one Xbox Series 1914 on BLE plus one Classic controller:
8BitDo Ultimate 2C (Switch mode), experimental Nintendo Switch Pro, or
experimental Sony DualShock 4. Xbox + DS4 is the goal of this development
stage and still requires physical validation. This is not an Xbox console
adapter.

SUPPORTED PAIR COMBINATIONS
Xbox + 8BitDo: physically validated previously.
Xbox + Switch Pro: one BLE plus one Classic transport; functioning alternative
confirmed by the user. This DS4 development stage does not repeat that test.
Xbox + DS4: candidate; physical validation pending.
8BitDo + Switch Pro, DS4 + 8BitDo, DS4 + Switch Pro, or two DS4 controllers:
NOT simultaneous. Both devices need the single Classic HID connection.
To change Classic controllers, turn off/disconnect the current one first.
The unused XInput output remains neutral. Two USB outputs do not mean two
Classic Bluetooth connections.

REMEMBERED BONDS VS ACTIVE CONNECTIONS
Up to four Classic identities and their link keys are remembered. Only one
Classic HID controller can be connected at a time. The Xbox uses the separate
single BLE connection. Two USB XInput outputs are fixed outputs, not extra
Bluetooth connections. BTstack reserves one additional temporary key record
only during a replacement transaction; it is not a fifth remembered bond.

When all four Classic identities are remembered, a new name/inquiry result
does not replace one automatically. Disconnect the active Classic controller,
load the CDC diagnostic firmware, open its COM port at 115200 baud, send
"BOND LIST", then send "BOND PAIR" and put the new controller into Pair mode
within 60 seconds. The Pico keeps the old bond until the new controller is
recognized and a valid controls report reaches INPUT_READY. A failed attempt
is rolled back. The least recently used disconnected, unreserved identity is
replaced only on success. The order is updated at INPUT_READY, not per packet.

To explicitly remove one bond, use "BOND REMOVE AA:BB:CC:DD:EE:FF" with the
address printed by BOND LIST. Only that Classic key and its RAM AUTOFIRE
settings are removed; the other Classic bonds and Xbox BLE bond remain. The
CDC diagnostic firmware has one generic HID output rather than dual XInput;
turn off the Xbox if that diagnostic output is occupied, then reload the
dual-XInput candidate after the pairing operation.

EXPERIMENTAL SWITCH PRO AND DUALSHOCK 4
Nintendo Switch Pro Controller support is a candidate. Hold its SYNC button for
first pairing. It is not considered ready until initialization completes and a
valid 0x30 controls report is decoded. Xbox + Switch Pro uses separate BLE and
Classic transports. Simultaneous 8BitDo + Switch Pro is not supported.

DualShock 4 is an experimental firmware profile. The user reports that its
controls and AUTOFIRE work in The Occultist. Small stick movement remains
visible in Windows joy.cpl; the supplied XInput capture does not include raw
Bluetooth reports. Xbox + DS4 simultaneous operation and the controller
revision have not been confirmed. For first pairing, turn it off, then hold SHARE and press/hold PS
until its light bar flashes. Keep the Pico on the DS4 CDC diagnostic firmware
and wait for discovery/authentication diagnostics. After bonding, press PS to
reconnect; do not hold SHARE for normal reconnect. The candidate identifies
DS4 using a HID descriptor signature and validates report ID/length, not name
alone. It accepts Bluetooth input report 0x01 (10 bytes) or 0x11 (78 bytes);
the full report's CRC32 is checked. SHARE + OPTIONS enters Autofire setup.
Cross/Circle/Square/Triangle map to Xbox A/B/X/Y. SHARE/OPTIONS map to
View/Menu. PS, touchpad, motion, audio, lightbar color and rumble are not
implemented. Xbox + DS4 remains pending a physical test.

DS4 REST-STICK DIAGNOSTIC
With the DS4 connected and INPUT_READY, leave it flat and untouched. Run
tools\capture-dualshock4.ps1 -Port COMx -RestOnly -RestSamples 24 from the
project folder. It requests one copied, correlated sample every 100 ms and
logs the full Bluetooth report, framing/Report ID, raw axis bytes, parser
result, CRC when applicable, normalized ControllerState and calculated
XInput axes. The callback copies into a bounded buffer; formatted logging
runs later from the timer. Check the omitted count at completion. This CDC
image calculates XInput values but exposes generic HID, not production XUSB.
It does not automatically calibrate the center.

PAIRING AND PLAYERS
Pair each controller once. Bluetooth bonds are retained across power cycles.
The first active controller that provides a valid input report uses the lowest
free logical player slot. Disconnecting neutralizes and releases only its slot;
the other controller is not moved. Windows XInput indices are global and can
differ when other controllers are installed.

AUTOFIRE SETUP — USE THE COMBINATION FOR YOUR CONTROLLER
Xbox Series: hold VIEW + MENU for 2 seconds.
8BitDo Ultimate 2C or Switch Pro: hold MINUS (-) + PLUS (+) for 2 seconds.
Sony DualShock 4 (PS4): hold SHARE + OPTIONS for 2 seconds.
For the DS4, SHARE + PS is only for first-time Bluetooth pairing; it does not
open AUTOFIRE setup. The Pico LED flashes three times when setup is armed.
Release both setup buttons before selecting a target. The illustration above
shows the setup buttons on all three controller types.

Press one target without the D-pad:
  Not configured -> enable at 10 presses/second (two short flashes)
  Already configured -> disable (one long flash)

Hold one cardinal D-pad direction and press exactly one target:
  UP = 5 presses/second       RIGHT = 10 presses/second
  DOWN = 15 presses/second    LEFT = 20 presses/second
The Pico LED flashes 1, 2, 3, or 4 times respectively.

Targets: A, B, X, Y, LB/RB (L/R), LT/RT (ZL/ZR), L3 and R3.
Diagonals and multiple simultaneous targets are rejected. Setup times out after
10 seconds. Hold the setup combo again for 2 seconds to cancel. Cancellation
flashes four times. Selection controls are consumed until released.

AUTOFIRE BEHAVIOUR
Autofire runs only while the physical target is held. A new press begins ON
immediately and alternates at 50 percent duty. Releasing stops immediately.
Trigger activation is 50 percent with release hysteresis at 45 percent.
Settings belong to each controller, are stored in RAM, survive its reconnect
during the current Pico session, and are erased when the Pico restarts.
Bluetooth bonds remain.

The first two seconds of VIEW/MENU or MINUS/PLUS are sent normally before the
firmware knows the combo is intentional. Configure while the game is paused.

SWITCH PHYSICAL BUTTON MAPPING
Physical B (bottom) -> Xbox A; A (right) -> Xbox B;
Y (left) -> Xbox X; X (top) -> Xbox Y.
Minus -> View, Plus -> Menu, L/R -> LB/RB, ZL/ZR -> LT/RT.
The 8BitDo triggers are digital in this mode. L4/R4 are not independent inputs
in the observed Bluetooth protocol; controller-side remaps act as their copied
control. Capture, advanced vibration, macros and profiles are not implemented.

LEDs
The Pico LED shows connection state and queued setup feedback; one shared LED
does not identify which controller requested feedback. The 8BitDo player lights
are separate and follow its current logical slot when its firmware accepts the
Switch player-light command.

RECOVERY
This PICO_GUIDE drive is static and read-only. Creating, editing, deleting,
renaming or formatting must fail. It is not the RPI-RP2 boot drive. For firmware
recovery, unplug the Pico, hold BOOTSEL while reconnecting, then copy the chosen
UF2 to RPI-RP2. Do not format either volume.

See MANUAL.HTM and AUTOFIRE.PNG for the complete offline reference.
