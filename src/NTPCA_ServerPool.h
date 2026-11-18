// ============================================================================
// NTPCA_ServerPool.h - up to three servers, queried one after another
// ============================================================================
#pragma once

#include "NTPCA_Config.h"
#include "NTPCA_Protocol.h"

#include <AsyncUDP.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace ntpca {

class ServerPool {
public:
    ServerPool();
    ~ServerPool();

    // Creates the RTOS primitives. Must succeed before queryServer() is used.
    bool init();
    void deinit();

    // Configuration (not safe while a query is running)
    void setServers(const char* srv1, const char* srv2 = nullptr, const char* srv3 = nullptr);
    void setTimeout(uint32_t timeoutMs);
    void setNetworkInterface(NTPNetworkInterface networkInterface) { _interface = networkInterface; }
    int serverCount() const { return _serverCount; }

    // One request/response exchange. Blocks the calling task for at most
    // DNS time + timeout. Returns true only for a valid reply.
    bool queryServer(int serverIdx);

    // Lets a running queryServer() return early (used by stop()).
    void requestAbort() { _abort = true; }
    void clearAbort() { _abort = false; }

    // Picks the best server among those whose latest query succeeded.
    void selectBestServer();
    int getBestServerIndex() const { return _bestServerIdx; }
    const NTPServerMetrics* getBestServer() const;
    const NTPServerMetrics* getMetrics(int& count) const;

    // The local clock was stepped by appliedMs: shift stored offsets so they
    // stay relative to the corrected clock.
    void applyCorrection(double appliedMs);

private:
    AsyncUDP _udp;
    NTPServerMetrics _servers[kMaxServers];
    int _serverCount;
    int _bestServerIdx;
    uint32_t _timeout;
    volatile NTPNetworkInterface _interface;
    volatile bool _abort;

    // Reply hand-over between the AsyncUDP task and the query task.
    SemaphoreHandle_t _mutex;     // protects the fields below
    SemaphoreHandle_t _rxSem;     // given when a matching reply arrived
    bool _waiting;
    uint32_t _txSec, _txFrac;     // our transmit timestamp, echoed as "origin"
    Packet _rx;
    uint64_t _rxT4;               // arrival time of the accepted reply

    void onPacket(AsyncUDPPacket& packet);
    void updateMetrics(int serverIdx, double offset, float delay, uint8_t stratum, uint32_t rootDisp);
    void updateReachability(int serverIdx, bool success);
};

}  // namespace ntpca
