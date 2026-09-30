# Restore Runbook

## A. Restore DynoTL Cloud license/app server

Use `server/cloudflare_current_946`.

1. Install Node.js LTS.
2. Open a terminal in that folder.
3. Run `npm install` — `node_modules` was intentionally excluded from the archive.
4. Confirm `wrangler.jsonc` points to the correct D1 database/binding.
5. If D1 must be recreated, run the provided deploy flow and apply `schema.sql`.
6. Recreate the three Cloudflare secrets listed in `SECURITY_SECRETS.md`.
7. Run `npx wrangler deploy` or the appropriate update/deploy BAT.
8. Verify `/api/ping`, `/api/version`, activation and heartbeat before sending launcher to customers.

### Important

The original signing seed must match the public key pinned in existing launchers. If that seed is lost, existing launchers may reject server signatures.

## B. Restore PC launcher/client

Preferred starting artifacts:

- Base/customer installer: `launcher/binaries/DynoTL_Setup_Universal_Monitor_v1.exe`
- Rescue clean install: `launcher/binaries/DynoTL_Clean_Install_Universal_Monitor_v3.exe`
- WebView fix candidate: `launcher/webview_fixes/DynoTL_WebView_Fix_v7_STA_STABLE.exe`

If a customer gets a white screen:

1. Run Diagnose v3 first.
2. Check local listener + Cloud server + WebView2.
3. If native WebView layer is the known failure, apply the newest verified WebView fix.
4. Use Clean Install v3 only when local install/cache/state needs to be reset.

## C. Restore app update process

Use the latest exact package in `updates/packages`.

For 946:

1. extract `DynoTL_Update_v1.2026.946_Compare_Expand_Color_FIX.zip`;
2. place BAT + `update_payload` in the Cloudflare server folder;
3. run `UPDATE_v1.2026.946_FORCE_VAR.bat`;
4. verify server `/api/version` reports `v1.2026.946`.

## D. Restore firmware server

The exact source package is not present. Use `server/firmware_server/README.md` as the contract and `CHECK_DYNOTL_FIRMWARE_SERVER.bat` as the first health check.

At minimum the Worker must return:

- valid metadata at `/latest.json`;
- exact binary at `/firmware.bin`;
- metadata fields compatible with the updater: `version`, `sha256`, `offset=65536`, size, target chip.

## E. Flash ESP32-S3 manually

The DynoTL updater writes only the application BIN to `0x10000` and preserves bootloader/partition table.

Known board configuration:

- ESP32S3 Dev Module
- CPU 240 MHz
- Flash 4MB
- QIO / 80 MHz
- PSRAM Disabled
- UART0/Hardware CDC upload

If bootloader connection fails: hold BOOT, tap RESET/EN, release RESET, release BOOT, then retry.