# Windows and hardware validation

## Load

Place Pico W in BOOTSEL and run:

```powershell
.\Load_Firmware.bat firmware\pico_xinput_dual_autofire_guide_candidate.uf2
```

Confirm the printed file and SHA-256. `RPI-RP2` disappears after the ROM copies the image.

## Required checks

1. Use `tools/check-xinput.ps1` to identify all Windows XInput indexes; do not assume the Pico owns global indexes 0 and 1.
2. Connect Xbox alone, then 8BitDo alone, and verify every mapped control and neutral state.
3. Connect both in each order, hold different controls simultaneously, and confirm independent output.
4. Disconnect and reconnect each controller without interrupting the other; verify lowest-free slot assignment and 8BitDo indicator.
5. Test five reconnect cycles per controller and a Pico power cycle.
6. Configure AUTOFIRE on one controller while playing with the other. Use `tools/measure-autofire.ps1` for 5/10/15/20 Hz and check its reported host polling cadence.
7. Open and copy all files on `PICO_GUIDE`; creating, deleting, renaming, or formatting must be rejected as write-protected. Do not run destructive disk tools.
8. Repeatedly copy `AUTOFIRE.PNG` while both gamepads are active.
9. Restart Pico and confirm bonds remain while AUTOFIRE settings are cleared.

The user has communicated successful physical validation of dual input, reconnect/bonds, slot release, 8BitDo player light, AUTOFIRE, and the read-only guide. Rebuilt binaries after documentation changes require a regression check because embedded guide bytes alter the UF2.
