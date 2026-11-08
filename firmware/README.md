# Firmware artifacts

Development UF2 files are intentionally excluded from the public repository and GitHub Releases. Build the current candidate locally with:

```powershell
.\setup-deps.ps1
.\build-autofire-guide.bat
```

The script creates `firmware/pico_xinput_dual_autofire_guide_candidate.uf2` and `Load_Firmware.bat` prints its SHA-256 before loading.

The current USB identity `2E8A:000A` is not assigned to this project. A production/distributable binary requires an authorized VID/PID and renewed Windows hardware validation. Binary distributors must also provide the notices described in [../THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).
