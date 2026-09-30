# Known Gaps / Pending Work

1. **Firmware server/uploader source missing** — only endpoint contract, old folder name and health checker survive.
2. **Monitor server patch source missing** — Monitor client binary survives; later presence/process server patch does not.
3. **WebView v7 needs user confirmation** — v6 definitely launched successfully but occasionally crashed. v7 is the newest STA-thread fix candidate.
4. **PC ESP32 FI false-pulse fix not committed** — 14k RPM / ~4286 µs limit discussed, surviving source remains 20k / 250 µs.
5. **Mobile charts need further optimization** — screenshots showed iPhone charts with dense/blurred labels and overlap; a planned 947 mobile-chart optimization was discussed but no exact 947 package exists in this archive.
6. **Cloud server current copy does not include monitor presence API** — keep license/app server stable until exact monitor patch is recovered or safely re-implemented.