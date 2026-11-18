// ============================================================================
// NTPClientAdvanced.h - multi-server NTP client for ESP32
//
// Runs in its own FreeRTOS task, queries up to three servers with AsyncUDP,
// scores them by delay / jitter / dispersion and sets the system clock from
// the best one.
// ============================================================================
#pragma once

#include "NTPCA_Config.h"
#include "NTPCA_Protocol.h"
#include "NTPCA_ServerPool.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <functional>
#include <time.h>

class NTPClientAdvanced {
public:
    // Callbacks run on the NTP task (not on loop()). Keep them short, do not
    // block, and note that the task stack is NTPCA_TASK_STACK bytes.
    typedef std::function<void(bool success)> SyncCallback;
    typedef std::function<void(const NTPServerMetrics* metrics, int count)> MetricsCallback;

    NTPClientAdvanced();
    ~NTPClientAdvanced();

    NTPClientAdvanced(const NTPClientAdvanced&) = delete;
    NTPClientAdvanced& operator=(const NTPClientAdvanced&) = delete;

    // === Configuration (call before begin(); setServers() only while stopped) ===
    void setServers(const char* server1, const char* server2 = nullptr, const char* server3 = nullptr);
    void setInterval(uint32_t intervalSeconds);
    void setTimeZone(int8_t hours, int8_t minutes = 0);
    void setTimeout(uint32_t timeoutMs);
    // AUTO (default): lwIP's default route. WIFI / ETHERNET: use only that interface.
    void setNetworkInterface(NTPNetworkInterface networkInterface);

    // === Control ===
    bool begin();
    void stop();
    void forceUpdate();
    bool isRunning() const { return _running; }

    // === Callbacks ===
    void onSyncEvent(SyncCallback callback);
    void onMetricsUpdate(MetricsCallback callback);

    // === Status ===
    bool isSynced() const { return _synced; }
    uint32_t getLastSync() const { return _lastSyncTime; }          // millis() of the last sync
    double getLastAdjustmentMs() const { return _lastAdjustmentMs; } // size of the last clock step
    NTPNetworkInterface getNetworkInterface() const { return _interface; }
    const NTPServerMetrics* getBestServer() const;
    const NTPServerMetrics* getMetrics(int& count) const;

    // === Time access ===
    // UTC epoch plus the configured timezone offset.
    time_t getEpochTime() const;
    // Formatted with strftime(); the configured timezone offset is applied.
    String getTimeString(const char* format = "%Y-%m-%d %H:%M:%S") const;

private:
    ntpca::ServerPool _pool;

    volatile bool _synced;
    volatile uint32_t _lastSyncTime;
    volatile double _lastAdjustmentMs;
    uint32_t _interval;
    int32_t _timezoneOffsetSec;
    NTPNetworkInterface _interface;

    SyncCallback _syncCallback;
    MetricsCallback _metricsCallback;

    TaskHandle_t _taskHandle;
    volatile bool _running;
    volatile bool _shouldStop;

    static void syncTask(void* param);
    void runLoop();
    bool performSync();
    bool applyBestServer();
};
