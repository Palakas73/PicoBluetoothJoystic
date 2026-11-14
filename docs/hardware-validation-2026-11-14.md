# Hardware validation record — 2026-11-14

## User-reported result

- The user tested the new DS4 controller with **The Occultist**.
- Controller controls and AUTOFIRE worked in that game.
- The user still observes stick movement/vibration in Windows `joy.cpl`.
- The supplied `check-xinput.ps1` capture for this controller showed small axis values near center (within about 5% of full travel); trigger values appeared in separate LT/RT fields. This XInput capture does not contain the Bluetooth HID reports.
- Xbox + DS4 simultaneous operation was not part of the reported game test. The DS4 revision and exact UF2 hash loaded during that game test were not independently recorded.

## Preserved firmware snapshot

The candidate present when this report was recorded has been copied to:

`firmware/pico_xinput_dual_ds4_autofire_validated_20261114.uf2`

SHA-256: `5BDD22ACC559FEFEF466F01C7C1B47990824449906CD2FEEC70E27207292C580`

This file is retained locally as a recovery artifact. UF2 files are ignored by Git, so the commit records the source and this hash, not the binary itself. The hardware report does not prove that this exact hash was the image loaded during the game test.

## Still unknown

- Whether the small stick movement originates in the DS4 or later in report interpretation/output. A CDC capture of the complete raw report is still needed to distinguish those cases.
- Whether Xbox BLE and DS4 Classic work simultaneously with this build.
- The original controller revision/model identity.
