# NTPClientAdvanced

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-ESP32-blue.svg)](https://www.espressif.com/en/products/socs/esp32)

A multi-server NTP client for ESP32. It queries up to three servers, ranks them by delay, jitter and dispersion, and sets the system clock from the best one. It works over WiFi and Ethernet and runs in its own FreeRTOS task, so `loop()` is never blocked.

It is an SNTP-style client: it uses the NTPv4 packet format (RFC 5905) in client mode, but it is not a full `ntpd` (no clock discipline loop, no peer clustering).

## Features

- Up to 3 servers, queried one after another, best one chosen by a quality score
- Background FreeRTOS task, with `forceUpdate()` and `stop()` taking effect immediately
- Retry with backoff after a failed sync (5 s, doubling up to 5 min)
- Replies accepted only if they echo the request's transmit timestamp (stray and spoofed packets are ignored)
- Correct maths for a cold-booted clock (1970) and across the 2036 NTP era rollover
- Per-server metrics: offset, delay, jitter, dispersion, stratum, reachability register
- Callbacks for sync result and metrics
- Timezone offset helper

## Requirements

- ESP32 (any variant), Arduino core 2.x or 3.x
- WiFi or Ethernet connection

`AsyncUDP` ships with the ESP32 Arduino core, so there is nothing else to install. ESP8266 is not supported (the library needs FreeRTOS tasks and the core's AsyncUDP).

## Installation

**PlatformIO**

```ini
[env:esp32]
platform = espressif32
framework = arduino
lib_deps = https://github.com/Nourbakhsh-Rad/NTPClientAdvanced.git
```

**Arduino IDE**: download the repository as a ZIP, then *Sketch → Include Library → Add .ZIP Library*.

## Quick start

```cpp
#include <WiFi.h>
#include <NTPClientAdvanced.h>

NTPClientAdvanced ntpClient;

void setup() {
    Serial.begin(115200);

    WiFi.begin("YOUR_SSID", "YOUR_PASSWORD");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }

    ntpClient.setServers("pool.ntp.org", "time.google.com");
    ntpClient.setInterval(3600);    // sync every hour
    ntpClient.setTimeZone(3, 30);   // UTC+3:30

    ntpClient.onSyncEvent([](bool success) {
        Serial.printf("NTP sync %s\n", success ? "OK" : "failed");
    });

    ntpClient.begin();
}

void loop() {
    if (ntpClient.isSynced()) {
        Serial.println(ntpClient.getTimeString());
    }
    delay(10000);
}
```

More in [`examples/`](examples): `BasicWiFi`, `EthernetLAN`, `ManualUpdate`, `WiFiEthernetFailover`.

## How it works

Every interval the NTP task resolves each server name, sends one request over a fresh UDP socket and waits for the reply. Each valid reply gives an offset and a round-trip delay (RFC 5905 formulas). Servers that answered in this round are scored, the best one wins, and its offset is added to the system clock with `settimeofday()`. The clock is **stepped**, not slewed.

If no server answers (network down, DNS failure, timeout) the next attempt comes after 5 s, then 10 s, 20 s ... up to 5 minutes, until a sync succeeds. After that the normal interval applies again.

## API reference

### Configuration

Call these before `begin()`. `setServers()` is ignored while the client is running; call `stop()` first.

| Method | Description |
| --- | --- |
| `setServers(s1, s2 = nullptr, s3 = nullptr)` | Up to 3 hostnames or IPs. If none are set, `pool.ntp.org` is used. |
| `setInterval(seconds)` | Sync interval, minimum 60 s, default 3600 s. |
| `setTimeout(ms)` | Wait per server, minimum 500 ms, default 2000 ms. |
| `setTimeZone(hours, minutes = 0)` | Offset from UTC, −12:00 to +14:00. The sign follows `hours` (or `minutes` when `hours` is 0): `(-3, 30)` is UTC−03:30. |
| `setNetworkInterface(NTPNetworkInterface::AUTO / WIFI / ETHERNET)` | Which interface sends the requests, default `AUTO`. See [Network interface](#network-interface). |

### Control

| Method | Description |
| --- | --- |
| `bool begin()` | Starts the background task. Returns `false` if already running or the task could not be created. |
| `void stop()` | Stops the task and waits for it to finish (up to 15 s). Safe to call from a callback. |
| `void forceUpdate()` | Wakes the task for an immediate sync. |
| `bool isRunning()` | `true` while the task is running. |

### Callbacks

Both run on the NTP task, not on `loop()`. Keep them short and non-blocking. The task stack is 6144 bytes (`NTPCA_TASK_STACK`).

```cpp
ntpClient.onSyncEvent([](bool success) { /* after every sync attempt */ });

ntpClient.onMetricsUpdate([](const NTPServerMetrics* metrics, int count) {
    for (int i = 0; i < count; i++) {
        Serial.printf("%s: delay %.1f ms, jitter %.2f ms\n",
                      metrics[i].server, metrics[i].delay, metrics[i].jitter);
    }
});
```

`onSyncEvent` receives `true` only if the system clock was actually set. `onMetricsUpdate` is called after every attempt, including failed ones, so you can see which servers did not answer.

### Status

| Method | Description |
| --- | --- |
| `bool isSynced()` | `true` once the clock was set at least once. |
| `uint32_t getLastSync()` | `millis()` at the last successful sync. |
| `double getLastAdjustmentMs()` | Size of the last clock step in ms. |
| `const NTPServerMetrics* getBestServer()` | Server used for the last sync, or `nullptr`. |
| `const NTPServerMetrics* getMetrics(int& count)` | All servers. The NTP task updates these in the background, so copy values you need to keep consistent. |

### Time

| Method | Description |
| --- | --- |
| `time_t getEpochTime()` | UTC epoch plus the configured timezone offset. |
| `String getTimeString(format)` | `strftime()` format, default `"%Y-%m-%d %H:%M:%S"`, timezone offset applied. |

The system clock itself stays in UTC. `getTimeString()` does not use the `TZ` environment variable, so it is not shifted twice if you also call `setenv("TZ", ...)` elsewhere; `%Z` and `%z` have no meaning in it.

## Server metrics

`NTPServerMetrics` holds, per server: `server`, `reachable`, `stratum`, `offset` (ms, double), `delay` (ms), `dispersion` (ms), `jitter` (ms), `lastSync`, `reach` (8-bit register, 1 = reply) and `failCount`.

```
quality() = delay / 2 + jitter + dispersion + failCount * 100      (lower is better)
```

Only servers whose latest query succeeded can be selected, so an old offset is never applied twice.

After a step, the stored offsets are re-based to the corrected clock, so `offset` shows the remaining difference (close to 0). The size of the step itself is in `getLastAdjustmentMs()`.

## Network interface

| Setting | Behaviour |
| --- | --- |
| `AUTO` (default) | lwIP picks the interface from its default route. |
| `WIFI` | Requests go out through the WiFi station interface only. |
| `ETHERNET` | Requests go out through the Ethernet interface only. |

With `AUTO`, if WiFi and Ethernet are both connected, the library does not choose: the ESP32 network stack does. Which one it prefers depends on the core version and on the order the links came up, so use `WIFI` or `ETHERNET` when it matters.

With `WIFI` or `ETHERNET`, the chosen interface must have an IPv4 address. If it has none, that sync attempt fails (and is retried with backoff); the other interface is **never** used as a fallback. DNS lookups still use the system resolver, so name resolution may go through the other link.

In every mode the reply comes back on the interface that sent the request, so there is no need to set a local IP address.

## Configuration macros

Override at build time, e.g. `build_flags = -DNTPCA_TASK_STACK=8192`.

| Macro | Default | Meaning |
| --- | --- | --- |
| `NTPCA_DEFAULT_INTERVAL_S` | 3600 | Default sync interval |
| `NTPCA_MIN_INTERVAL_S` | 60 | Shortest accepted interval |
| `NTPCA_DEFAULT_TIMEOUT_MS` | 2000 | Default per-server timeout |
| `NTPCA_MIN_TIMEOUT_MS` | 500 | Shortest accepted timeout |
| `NTPCA_SERVER_GAP_MS` | 200 | Pause between two servers |
| `NTPCA_MAX_DELAY_MS` | 10000 | Replies with a longer round trip are rejected |
| `NTPCA_RETRY_MIN_S` / `NTPCA_RETRY_MAX_S` | 5 / 300 | Retry backoff range |
| `NTPCA_TASK_STACK` | 6144 | NTP task stack in bytes |
| `NTPCA_TASK_PRIORITY` | 1 | NTP task priority |

## Known limitations

- Maximum 3 servers.
- The clock is stepped with `settimeofday()`, so it can jump by the measured offset. Code that needs a monotonic clock should use `millis()` or `esp_timer_get_time()`.
- Do not run it together with Arduino's `configTime()` / SNTP; both would set the clock.
- DNS lookups block the NTP task for as long as the resolver takes.
- IPv4 only.
- Metrics are not stored across reboots.
- Do not poll pool servers more often than needed; the default hour is a polite value.

## Troubleshooting

- **Never syncs**: check `WiFi.status()` / Ethernet link (with a forced interface, that exact interface needs an IP), then use `onMetricsUpdate` to see which servers are unreachable. A firewall blocking outgoing UDP port 123 gives exactly this symptom.
- **High delay or jitter**: use regional pools (`asia.pool.ntp.org`, `ir.pool.ntp.org`) and raise `setTimeout()`.
- **Stack problems in callbacks**: raise `NTPCA_TASK_STACK`, or do the heavy work in `loop()` and only set a flag in the callback.

## Contributing

Issues and pull requests are welcome. Please keep changes small and describe how you tested them on hardware.

## Author

[Sh. Nourbakhsh Rad](mailto:Sh.Nourbakhsh@gmail.com)

## Acknowledgments

Derived from [ESPNtpClient](https://github.com/gmag11/ESPNtpClient) by gmag11 (MIT License). Its copyright notice is kept in [LICENSE](LICENSE).

## References

- [RFC 5905: Network Time Protocol Version 4](https://www.rfc-editor.org/rfc/rfc5905)
- [NTP Pool Project](https://www.ntppool.org/)
- [ESP32 Arduino core](https://github.com/espressif/arduino-esp32)

## License

MIT, see [LICENSE](LICENSE).
