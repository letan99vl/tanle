# Current State — 2026-10-01

## Versions

| Component | Current surviving/latest state | Confidence |
|---|---|---|
| PC encrypted app | v1.2026.946 | High — exact update package survives |
| Cloud server source | 943-era source + current 946 payload recovery copy | High for license/app server; monitor patch source missing |
| Universal Monitor installer | v1 | High — EXE survives |
| Clean rescue installer | v3 | High — EXE survives |
| WebView native fix | v7 STA Stable candidate | Medium — v6 user-confirmed working with occasional crash; v7 not re-confirmed in chat |
| Base RememberKey installer | v1.2026.942 | High — original RAR survives |
| Firmware updater UI | present in 943+ app and 946 payload | High |
| Firmware server/uploader source | v5_401_FIX mentioned previously, exact source missing | Low/source missing |
| Mobile repo | `letan99vl/tanle` main | High — live GitHub source |

## App update chronology kept here

- `943` — firmware updater module placement fix / baseline used to build later updates.
- `944` — Compare chart zoom/pan experiment; Expand button injection had a placement issue.
- `945` — fixed four visible Expand buttons on Power/Torque/Speed/AFR comparison charts.
- `946` — fixed expanded comparison chart colors so Run colors match the normal History view.

## WebView chronology

- v1 — Edge app-shell compatibility test.
- v2 — attempted native-looking embedding, still visibly browser-like.
- v3 — Edge kiosk; still browser process/icon and caused `Failed to fetch`.
- v4 — first native attempt; failed with `HRESULT 0x80070032`.
- v5 — official `WebView2Loader.dll` path; loaded but could stall.
- v6 — fixed COM object lifetime; **user confirmed it runs**, but it could occasionally crash.
- v7 — pins all WebView2/COM work to one STA OS thread and adds better crash logging; newest candidate.

## What not to assume

- Do **not** assume the monitor presence/process server patch is inside `cloudflare_current_946`; the recovered server source predates/does not show those endpoints.
- Do **not** assume the surviving ESP32 source already has the later 14,000 RPM false-pulse gate. The surviving file still contains `ENGINE_MIN_PERIOD_US = 250` and `MAX_RPM_ENGINE = 20000`.
- Do **not** overwrite Blink-Redleo when working on DynoTL.