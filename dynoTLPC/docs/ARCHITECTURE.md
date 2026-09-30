# DynoTLPC Architecture

## 1. PC client chain

`DynoTL.exe (launcher)` → `DynoTLCore.exe / WebView host` → local HTTP page → encrypted app payload received from DynoTL Cloud.

The launcher/client authenticates a license key and HWID against the Cloudflare Worker. The Worker encrypts the app payload per activation session. Heartbeat keeps the session alive.

### Main client files seen in a working installation

- `DynoTL.exe` — customer-facing launcher.
- `DynoTLCore.exe` — local host / app rendering core.
- `DynoTLMonitor.exe` — optional presence/process monitor companion.
- `DynoTL_Setup.exe` — bundled/base setup used by universal installers.
- `server_url.txt` — points launcher to `https://dynotl-cloud.dynotl-quyen.workers.dev`.
- `VERSION.txt`, `LEGAL_NOTICE.txt`, `MONITOR_NOTICE.txt` — metadata/notices.

## 2. License + app Cloudflare server

Worker: `dynotl-cloud`  
Public URL: `https://dynotl-cloud.dynotl-quyen.workers.dev`

Core endpoints recovered from source:

- `GET /` or `/api/ping` — health + app version.
- `POST /api/activate` — license activation, HWID binding, encrypted payload delivery.
- `POST /api/heartbeat` — session validity.
- `GET /api/version` — current app version.
- `/admin` — license admin page.
- `/admin/api/*` — create/list/revoke/unrevoke/kick/reset/delete licenses.

Storage: Cloudflare D1 binding `DB`, database name `dynotl-cloud`.

Cryptography in recovered source:

- X25519 ECDH
- AES-256-GCM payload encryption
- Ed25519 server signature
- HMAC/session secret
- HWID binding

## 3. App update path

Encrypted `src/app_payload.html` is served by the Worker after activation. Update BAT files:

1. back up old `src/app_payload.html`;
2. copy `update_payload/app_payload.html` into `src/`;
3. bump `APP_VERSION` in `wrangler.jsonc`;
4. deploy with Wrangler.

Newest package in this archive: **v1.2026.946**.

## 4. ESP32-S3 firmware update path

DynoTL Settings firmware updater:

`latest.json` → metadata validation → `firmware.bin` → SHA-256 validation → esptool-js → write app image to `0x10000`.

Known URLs:

- `https://dynotl-firmware.dynotl-quyen.workers.dev/latest.json`
- `https://dynotl-firmware.dynotl-quyen.workers.dev/firmware.bin`
- `https://unpkg.com/esptool-js@0.6.1/bundle.js`

Known Cloudflare configuration from prior work:

- Worker: `dynotl-firmware`
- KV namespace: `DYNOTL_FIRMWARE`
- KV binding: `FIRMWARE_KV`

## 5. PC ESP32 data stream

Surviving source uses:

- GPIO18 — roller/wheel Hall
- GPIO16 — engine RPM pickup
- GPIO4 — AFR analog
- serial output every ~25 ms:

`D,timeMs,rollerRPM,engineRPM,afrVoltage`

## 6. Mobile lane

GitHub repo `letan99vl/tanle` contains DynoTL Mobile/PWA + Android wrapper + BLE ESP32-S3 firmware. It is related but should remain a separate lane from the PC Cloud launcher/server.