# Changelog

## 1.0.0

First public release.

- Multi-server NTP client for ESP32 (up to three servers, queried one after another).
- Best-server selection from delay, jitter, dispersion and failure count.
- Runs in its own FreeRTOS task; `forceUpdate()` and `stop()` take effect immediately.
- Automatic retry with backoff (5 s, doubling up to 5 min) after a failed sync.
- Replies are accepted only if they echo the request's transmit timestamp.
- Correct offset maths across the NTP era rollover (2036) and for a cold-booted clock.
- Selectable network interface: `AUTO` (lwIP default route), `WIFI` or `ETHERNET` (no fallback to the other link).
- Callbacks for sync result and per-server metrics.
- Timezone offset helper (`setTimeZone`), `getEpochTime()`, `getTimeString()`.
- Examples: BasicWiFi, EthernetLAN, ManualUpdate, WiFiEthernetFailover.
