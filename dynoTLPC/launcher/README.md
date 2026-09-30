# Launcher / Installer Files

## Customer/base path

- `binaries/DynoTL_Setup_Universal_Monitor_v1.exe` — one-file universal installer that was built to install from a blank machine or add Monitor to an existing client.
- `binaries/DynoTL_Chuyen_Sang_Cloud.exe` — earlier launcher used to move DynoTL to the Cloudflare server.

## Rescue clean install

- v1/v2 — intermediate rescue attempts.
- `DynoTL_Clean_Install_Universal_Monitor_v3.exe` — newest surviving one-hit clean install rescue. Intended to remove old local DynoTL state/cache and reinstall.

## WebView fixes

Keep all versions for forensic fallback. Do not distribute v1–v5 as current fixes. v6 was user-confirmed to run but sometimes crash. v7 is the newest STA-thread candidate.

## Diagnostics

- Diagnose v3 is the preferred diagnostic artifact in this archive.
- `CHECK_DYNOTL_FIRMWARE_SERVER.bat` tests metadata, BIN and esptool availability.