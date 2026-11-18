// ============================================================================
// NTPCA_Config.h - constants, public enums and the per-server metrics struct
// ============================================================================
#pragma once

#if !defined(ARDUINO_ARCH_ESP32)
#error "NTPClientAdvanced supports ESP32 only (it needs FreeRTOS and AsyncUDP from the ESP32 Arduino core)."
#endif

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Tunables. Each can be overridden with a build flag, e.g.
//   build_flags = -DNTPCA_TASK_STACK=8192
// ---------------------------------------------------------------------------
#ifndef NTPCA_DEFAULT_INTERVAL_S
#define NTPCA_DEFAULT_INTERVAL_S 3600   // default sync interval (1 hour)
#endif
#ifndef NTPCA_MIN_INTERVAL_S
#define NTPCA_MIN_INTERVAL_S 60         // shortest interval setInterval() accepts
#endif
#ifndef NTPCA_DEFAULT_TIMEOUT_MS
#define NTPCA_DEFAULT_TIMEOUT_MS 2000   // wait for one server's reply
#endif
#ifndef NTPCA_MIN_TIMEOUT_MS
#define NTPCA_MIN_TIMEOUT_MS 500        // shortest timeout setTimeout() accepts
#endif
#ifndef NTPCA_SERVER_GAP_MS
#define NTPCA_SERVER_GAP_MS 200         // pause between two server queries
#endif
#ifndef NTPCA_MAX_DELAY_MS
#define NTPCA_MAX_DELAY_MS 10000        // replies with a longer round trip are rejected
#endif
#ifndef NTPCA_RETRY_MIN_S
#define NTPCA_RETRY_MIN_S 5             // first retry after a failed sync
#endif
#ifndef NTPCA_RETRY_MAX_S
#define NTPCA_RETRY_MAX_S 300           // retry delay doubles up to this value
#endif
#ifndef NTPCA_TASK_STACK
#define NTPCA_TASK_STACK 6144           // bytes; user callbacks run on this stack
#endif
#ifndef NTPCA_TASK_PRIORITY
#define NTPCA_TASK_PRIORITY 1
#endif
#ifndef NTPCA_STOP_WAIT_MS
#define NTPCA_STOP_WAIT_MS 15000        // how long stop() waits for the task to exit
#endif

namespace ntpca {
constexpr uint16_t kPort = 123;
constexpr size_t kPacketSize = 48;
constexpr uint64_t kUnixEpochOffset = 2208988800ULL;  // seconds from 1900 to 1970
constexpr int kMaxServers = 3;
}  // namespace ntpca

// Which network interface carries the NTP requests.
//   AUTO     - let lwIP pick the interface from its default route (default).
//   WIFI     - send through the WiFi station interface only.
//   ETHERNET - send through the Ethernet interface only.
// With WIFI or ETHERNET the sync fails (and is retried) while that interface
// has no IP address; the other interface is never used as a fallback.
enum class NTPNetworkInterface : uint8_t {
    AUTO,
    WIFI,
    ETHERNET
};

// Quality information collected for one server.
struct NTPServerMetrics {
    char server[64];         // hostname or IP as configured
    bool reachable;          // answered at least once in the last 8 queries
    uint8_t stratum;         // stratum reported by the server (1-15)
    double offset;           // clock offset in ms (server - local); ~0 after a sync, see getLastAdjustmentMs()
    float delay;             // round-trip delay in ms
    float dispersion;        // root dispersion reported by the server, ms
    float jitter;            // smoothed change of the offset between syncs, ms
    uint32_t lastSync;       // millis() of the last valid reply
    uint8_t reach;           // 8-bit reachability register (1 = reply, 0 = miss)
    uint16_t failCount;      // consecutive failures

    // Lower is better.
    float quality() const {
        if (!reachable || stratum == 0 || stratum > 15) {
            return 999999.0f;
        }
        return (delay / 2.0f) + jitter + dispersion + (float)failCount * 100.0f;
    }

    void reset() {
        server[0] = '\0';
        reachable = false;
        stratum = 0;
        offset = 0.0;
        delay = 0.0f;
        dispersion = 0.0f;
        jitter = 0.0f;
        lastSync = 0;
        reach = 0;
        failCount = 0;
    }
};
