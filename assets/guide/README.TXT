PicoBluetoothJoystic - Quick Guide
==================================

SUPPORTED HARDWARE
Raspberry Pi Pico W with one Xbox Series 1914 controller over BLE and one
8BitDo Ultimate 2C Bluetooth controller in Nintendo Switch mode over Classic
HID. The PC sees two fixed XInput outputs. This is not an Xbox console adapter.
Support for two Xbox controllers, two 8BitDo controllers, or other models has
not been implemented or validated.

PAIRING AND PLAYERS
Pair each controller once. Bluetooth bonds are retained across power cycles.
The first active controller that provides a valid input report uses the lowest
free logical player slot. Disconnecting neutralizes and releases only its slot;
the other controller is not moved. Windows XInput indices are global and can
differ when other controllers are installed.

AUTOFIRE SETUP
Xbox: hold VIEW + MENU for 2 seconds.
8BitDo: hold MINUS (-) + PLUS (+) for 2 seconds.
The Pico LED flashes three times. Release both setup buttons before selecting.

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
