# dynoTLPC — Master Project Archive

Snapshot date: **2026-10-01**  
Owner: **letan99vl**  
Purpose: one place to recover, understand and continue the DynoTL PC ecosystem after a long break.

## Current known state

- **PC app payload:** `v1.2026.946` is the newest surviving/deployed package in this archive.
- **Cloudflare license/app server:** base source recovered from the 943-era server backup; `server/cloudflare_current_946` is a cleaned working copy with the app payload/version advanced to 946.
- **PC launcher / monitor:** `DynoTL_Setup_Universal_Monitor_v1.exe` is the one-file universal installer that bundles the base DynoTL setup + monitor logic.
- **WebView repair:** v6 was confirmed by the user to run, but could occasionally crash. `DynoTL_WebView_Fix_v7_STA_STABLE.exe` is the newest fix candidate designed to keep WebView2 COM on one STA OS thread; **user confirmation of v7 stability was not captured before work moved on**.
- **Rescue clean install:** `DynoTL_Clean_Install_Universal_Monitor_v3.exe` is the newest surviving one-hit clean-install rescue package.
- **Firmware updater inside DynoTL:** downloads metadata from `https://dynotl-firmware.dynotl-quyen.workers.dev/latest.json`, downloads `firmware.bin`, verifies SHA-256 and flashes the ESP32-S3 app image at **0x10000**.
- **Firmware server source:** the exact `DynoTL_Firmware_Server_and_Uploader_v5_401_FIX` source package is **not present in the surviving files**. Its contract and known Cloudflare/KV configuration are documented under `server/firmware_server/README.md`.
- **Monitor server patch:** the later presence/process-monitor server patch was deployed in prior work, but the exact source package is **not present in the surviving local files**. The intended API/behavior is documented under `docs/MONITOR_STATUS.md` so it can be recovered without guessing.

## Read in this order

1. `docs/ARCHITECTURE.md` — what talks to what.
2. `docs/CURRENT_STATE.md` — what is current vs historical.
3. `docs/RESTORE_RUNBOOK.md` — how to rebuild the system from zero.
4. `docs/FILE_CATALOG.md` — every file in this archive with size + SHA-256 + purpose.
5. `docs/SECURITY_SECRETS.md` — secrets that must never be committed/shared.
6. `docs/KNOWN_GAPS_AND_PENDING.md` — things discussed but not yet safely committed.

## Folder map

- `server/cloudflare_snapshot_943/` — cleaned exact server snapshot recovered from the RAR backup. No secrets, no node_modules, no .wrangler cache.
- `server/cloudflare_current_946/` — working recovery copy, same server source with current 946 encrypted app payload and APP_VERSION set to 946.
- `server/firmware_server/` — surviving diagnostic script + documented firmware server contract.
- `launcher/` — universal installer, rescue installers, diagnostics, WebView repair binaries.
- `firmware/esp32_pc/` — surviving PC ESP32 source used for roller RPM / engine RPM / AFR serial stream.
- `updates/` — update packages 944–946 and decrypted QA/reference payloads.
- `raw_archives/` — untouched original RAR backups. Keep these forever.
- `github_snapshot/` — GitHub repository state observed when this archive was built.

## Important separation

`letan99vl/Blink-Redleo` is a separate ECU/Redleo project. DynoTL borrowed some Android/BLE patterns from it, but the whole Redleo project must **not** be merged blindly into DynoTLPC.