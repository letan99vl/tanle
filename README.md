# Dyno TL Mobile

Mobile-first dyno dashboard for ESP32-S3 using Bluetooth Low Energy (BLE).

- 5 screens: Live, HP/Torque, RPM/AFR, 201 m, Settings.
- Compact controls for landscape phones.
- Portrait phones render the app rotated as a landscape canvas.
- PWA manifest requests fullscreen + landscape.
- Web Bluetooth follows the same browser/GATT approach used in Blink-Redleo.

## BLE
Device name: `DynoTL Mobile Hardware`

Service: `d7a10001-7c35-4a6d-9f0e-2ea3117f1000`

LIVE notify: `d7a10002-7c35-4a6d-9f0e-2ea3117f1000`

COMMAND write: `d7a10003-7c35-4a6d-9f0e-2ea3117f1000`

STATUS notify: `d7a10005-7c35-4a6d-9f0e-2ea3117f1000`

LIVE samples are UTF-8 lines:

`D,timeMs,rollerRPM,engineRPM,afrVoltage`

The provided ESP32-S3 sketch is the BLE transport layer. Merge your existing sensor acquisition code into it and call:

`dynoPublishSample(millis(), rollerRPM, engineRPM, afrVoltage);`

## iPhone
Safari does not expose Web Bluetooth. Use Bluefy or another Web Bluetooth-capable iOS browser.

Pages deploy trigger: 2026-09-29
