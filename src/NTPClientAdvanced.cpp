// ============================================================================
// NTPClientAdvanced.cpp
// ============================================================================
#include "NTPClientAdvanced.h"

#include <freertos/task.h>
#include <math.h>

namespace {

// Seconds -> RTOS ticks without the 32-bit overflow that pdMS_TO_TICKS()
// hits for long intervals (e.g. a daily sync at 100 Hz).
TickType_t secondsToTicks(uint32_t seconds) {
    uint64_t ticks = (uint64_t)seconds * (uint64_t)configTICK_RATE_HZ;
    if (ticks > (uint64_t)(portMAX_DELAY - 1)) {
        ticks = (uint64_t)(portMAX_DELAY - 1);
    }
    return (TickType_t)ticks;
}

}  // namespace

NTPClientAdvanced::NTPClientAdvanced()
    : _synced(false)
    , _lastSyncTime(0)
    , _lastAdjustmentMs(0.0)
    , _interval(NTPCA_DEFAULT_INTERVAL_S)
    , _timezoneOffsetSec(0)
    , _interface(NTPNetworkInterface::AUTO)
    , _taskHandle(nullptr)
    , _running(false)
    , _shouldStop(false)
{
}

NTPClientAdvanced::~NTPClientAdvanced() {
    stop();
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
void NTPClientAdvanced::setServers(const char* server1, const char* server2, const char* server3) {
    if (_running) {
        return;                       // the NTP task is reading the server list
    }
    _pool.setServers(server1, server2, server3);
}

void NTPClientAdvanced::setInterval(uint32_t intervalSeconds) {
    _interval = (intervalSeconds < NTPCA_MIN_INTERVAL_S) ? (uint32_t)NTPCA_MIN_INTERVAL_S : intervalSeconds;
}

void NTPClientAdvanced::setTimeZone(int8_t hours, int8_t minutes) {
    // The sign of the offset comes from `hours` (or from `minutes` when hours
    // is 0), so (-3, 30) means UTC-03:30 and (5, 45) means UTC+05:45.
    int32_t h = hours;
    int32_t m = minutes;
    const bool negative = (h < 0) || (h == 0 && m < 0);
    if (h < 0) h = -h;
    if (m < 0) m = -m;
    if (m > 59) m = 59;

    int32_t total = h * 3600 + m * 60;
    if (negative) {
        total = -total;
    }
    if (total < -12 * 3600) total = -12 * 3600;
    if (total > 14 * 3600) total = 14 * 3600;
    _timezoneOffsetSec = total;
}

void NTPClientAdvanced::setTimeout(uint32_t timeoutMs) {
    _pool.setTimeout(timeoutMs);
}

void NTPClientAdvanced::setNetworkInterface(NTPNetworkInterface networkInterface) {
    _interface = networkInterface;
    _pool.setNetworkInterface(networkInterface);
}

void NTPClientAdvanced::onSyncEvent(SyncCallback callback) {
    _syncCallback = callback;
}

void NTPClientAdvanced::onMetricsUpdate(MetricsCallback callback) {
    _metricsCallback = callback;
}

// ---------------------------------------------------------------------------
// Control
// ---------------------------------------------------------------------------
bool NTPClientAdvanced::begin() {
    if (_running) {
        return false;                 // already started
    }
    if (!_pool.init()) {
        return false;
    }
    if (_pool.serverCount() == 0) {
        _pool.setServers("pool.ntp.org");
    }

    _shouldStop = false;
    _running = true;
    const BaseType_t result = xTaskCreate(syncTask, "NTPSync", NTPCA_TASK_STACK, this,
                                          NTPCA_TASK_PRIORITY, &_taskHandle);
    if (result != pdPASS) {
        _running = false;
        _taskHandle = nullptr;
        return false;
    }
    return true;
}

void NTPClientAdvanced::stop() {
    if (_running) {
        _shouldStop = true;
        _pool.requestAbort();
        if (_taskHandle) {
            xTaskNotifyGive(_taskHandle);        // wake the task if it is sleeping
        }
        if (xTaskGetCurrentTaskHandle() == _taskHandle) {
            return;                              // called from one of our own callbacks:
                                                 // the task ends when the callback returns
        }
        // The task clears _running itself just before it deletes itself.
        uint32_t waited = 0;
        while (_running && waited < NTPCA_STOP_WAIT_MS) {
            vTaskDelay(pdMS_TO_TICKS(20));
            waited += 20;
        }
    }
    _taskHandle = nullptr;
    _pool.deinit();
}

void NTPClientAdvanced::forceUpdate() {
    if (_running && _taskHandle) {
        xTaskNotifyGive(_taskHandle);
    }
}

// ---------------------------------------------------------------------------
// NTP task
// ---------------------------------------------------------------------------
void NTPClientAdvanced::syncTask(void* param) {
    NTPClientAdvanced* self = static_cast<NTPClientAdvanced*>(param);
    self->runLoop();
    self->_running = false;               // after this line `self` may be destroyed
    vTaskDelete(nullptr);
}

void NTPClientAdvanced::runLoop() {
    uint32_t retryS = NTPCA_RETRY_MIN_S;
    bool ok = performSync();

    while (!_shouldStop) {
        uint32_t waitS;
        if (ok) {
            retryS = NTPCA_RETRY_MIN_S;
            waitS = _interval;
        } else {
            // Failed (network down, DNS, no reply): retry sooner, with backoff.
            waitS = (retryS < _interval) ? retryS : _interval;
            retryS = (retryS * 2 > (uint32_t)NTPCA_RETRY_MAX_S) ? (uint32_t)NTPCA_RETRY_MAX_S : retryS * 2;
        }

        // Sleeps until the interval passes, forceUpdate() or stop() notifies us.
        ulTaskNotifyTake(pdTRUE, secondsToTicks(waitS));
        if (_shouldStop) {
            break;
        }
        ok = performSync();
    }
}

bool NTPClientAdvanced::performSync() {
    const int count = _pool.serverCount();
    bool anyReply = false;

    for (int i = 0; i < count && !_shouldStop; i++) {
        if (_pool.queryServer(i)) {
            anyReply = true;
        }
        if (i < count - 1 && !_shouldStop) {
            vTaskDelay(pdMS_TO_TICKS(NTPCA_SERVER_GAP_MS));
        }
    }
    if (_shouldStop) {
        return false;
    }

    _pool.selectBestServer();
    const bool applied = anyReply && applyBestServer();

    int n = 0;
    const NTPServerMetrics* metrics = _pool.getMetrics(n);
    if (_metricsCallback) {
        _metricsCallback(metrics, n);
    }
    if (_syncCallback) {
        _syncCallback(applied);
    }
    return applied;
}

bool NTPClientAdvanced::applyBestServer() {
    const NTPServerMetrics* best = _pool.getBestServer();
    if (!best) {
        return false;
    }

    const double offsetMs = best->offset;
    if (!ntpca::Protocol::stepClock(offsetMs)) {
        return false;
    }

    _pool.applyCorrection(offsetMs);
    _lastAdjustmentMs = offsetMs;
    _lastSyncTime = millis();
    _synced = true;
    return true;
}

// ---------------------------------------------------------------------------
// Status / time
// ---------------------------------------------------------------------------
const NTPServerMetrics* NTPClientAdvanced::getBestServer() const {
    return _pool.getBestServer();
}

const NTPServerMetrics* NTPClientAdvanced::getMetrics(int& count) const {
    return _pool.getMetrics(count);
}

time_t NTPClientAdvanced::getEpochTime() const {
    return time(nullptr) + _timezoneOffsetSec;
}

String NTPClientAdvanced::getTimeString(const char* format) const {
    char buffer[64];
    const time_t shifted = getEpochTime();
    struct tm timeinfo;
    // The offset is already in `shifted`, so read it as UTC. This keeps the
    // result independent of any TZ environment variable set elsewhere.
    gmtime_r(&shifted, &timeinfo);
    if (!format || strftime(buffer, sizeof(buffer), format, &timeinfo) == 0) {
        buffer[0] = '\0';
    }
    return String(buffer);
}
