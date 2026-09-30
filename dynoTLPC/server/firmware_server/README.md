# ESP32-S3 Firmware Server Contract

Known production Worker URL:

`https://dynotl-firmware.dynotl-quyen.workers.dev`

Known prior setup:

- Worker: `dynotl-firmware`
- KV namespace: `DYNOTL_FIRMWARE`
- binding: `FIRMWARE_KV`
- uploader tool/folder name remembered from prior work: `DynoTL_Firmware_Server_and_Uploader_v5_401_FIX`
- uploader entry point: `START_FIRMWARE_UPLOADER.bat`

The exact source of that uploader/server package did not survive in currently mounted files.

## Client contract from app v1.2026.946

Metadata:

`GET /latest.json`

Binary:

`GET /firmware.bin`

Metadata must provide at least:

- `version`
- `sha256`
- `offset` = `0x10000` / 65536
- optional `chip` containing `ESP32-S3`
- size/fileName/uploadedAt are displayed by UI when present

Client rejects:

- missing metadata/version/SHA;
- wrong offset;
- wrong chip;
- empty BIN;
- BIN larger than 1,310,720 bytes;
- BIN without ESP image header `0xE9`;
- SHA-256 mismatch.

Flash settings used by the app:

- baud: 460800
- address: 0x10000
- flashSize: 4MB
- flashMode: qio
- flashFreq: 80m
- eraseAll: false
- compress: true

Use `CHECK_DYNOTL_FIRMWARE_SERVER.bat` to distinguish firmware server failure from Web Serial/BOOT problems.