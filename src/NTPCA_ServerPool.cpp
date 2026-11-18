// ============================================================================
// NTPCA_ServerPool.cpp
// ============================================================================
#include "NTPCA_ServerPool.h"

#include <WiFi.h>
#include <esp_netif.h>
#include <math.h>
#include <string.h>

namespace ntpca {

namespace {

// True if the given interface exists and has an IPv4 address.
bool interfaceHasIp(NTPNetworkInterface which) {
    const char* key = (which == NTPNetworkInterface::WIFI) ? "WIFI_STA_DEF" : "ETH_DEF";
    esp_netif_t* handle = esp_netif_get_handle_from_ifkey(key);
    if (!handle) {
        return false;
    }
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(handle, &info) != ESP_OK) {
        return false;
    }
    return info.ip.addr != 0;
}

}  // namespace

ServerPool::ServerPool()
    : _serverCount(0)
    , _bestServerIdx(-1)
    , _timeout(NTPCA_DEFAULT_TIMEOUT_MS)
    , _interface(NTPNetworkInterface::AUTO)
    , _abort(false)
    , _mutex(nullptr)
    , _rxSem(nullptr)
    , _waiting(false)
    , _txSec(0)
    , _txFrac(0)
    , _rxT4(0)
{
    for (int i = 0; i < kMaxServers; i++) {
        _servers[i].reset();
    }
    _rx.clear();
}

ServerPool::~ServerPool() {
    deinit();
    if (_mutex) {
        vSemaphoreDelete(_mutex);
        _mutex = nullptr;
    }
    if (_rxSem) {
        vSemaphoreDelete(_rxSem);
        _rxSem = nullptr;
    }
}

bool ServerPool::init() {
    if (!_mutex) {
        _mutex = xSemaphoreCreateMutex();
    }
    if (!_rxSem) {
        _rxSem = xSemaphoreCreateBinary();
    }
    _abort = false;
    return _mutex != nullptr && _rxSem != nullptr;
}

void ServerPool::deinit() {
    _udp.close();
}

void ServerPool::setServers(const char* srv1, const char* srv2, const char* srv3) {
    const char* list[kMaxServers] = {srv1, srv2, srv3};

    _serverCount = 0;
    _bestServerIdx = -1;
    for (int i = 0; i < kMaxServers; i++) {
        _servers[i].reset();
    }
    for (int i = 0; i < kMaxServers; i++) {
        if (list[i] && list[i][0] != '\0') {
            strncpy(_servers[_serverCount].server, list[i], sizeof(_servers[0].server) - 1);
            _servers[_serverCount].server[sizeof(_servers[0].server) - 1] = '\0';
            _serverCount++;
        }
    }
}

void ServerPool::setTimeout(uint32_t timeoutMs) {
    _timeout = (timeoutMs < NTPCA_MIN_TIMEOUT_MS) ? (uint32_t)NTPCA_MIN_TIMEOUT_MS : timeoutMs;
}

// Runs on the AsyncUDP task. Only a reply that echoes our transmit timestamp
// is accepted (RFC 5905 section 8), so stray, late or spoofed packets are
// ignored. The socket is "connected" to the server, so lwIP already filters
// on the source address and port.
void ServerPool::onPacket(AsyncUDPPacket& packet) {
    if (packet.length() != kPacketSize || !_mutex) {
        return;
    }
    const uint64_t t4 = Protocol::nowNtpUs();   // stamp as early as possible

    Packet pkt;
    pkt.fromBytes(packet.data());

    bool accepted = false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (_waiting && pkt.origTm_s == _txSec && pkt.origTm_f == _txFrac) {
            _rx = pkt;
            _rxT4 = t4;
            _waiting = false;
            accepted = true;
        }
        xSemaphoreGive(_mutex);
    }
    if (accepted) {
        xSemaphoreGive(_rxSem);
    }
}

bool ServerPool::queryServer(int serverIdx) {
    if (serverIdx < 0 || serverIdx >= _serverCount || !_mutex || !_rxSem) {
        return false;
    }

    // 0. A forced interface must be up, otherwise lwIP could quietly use another.
    const NTPNetworkInterface wanted = _interface;
    if (wanted != NTPNetworkInterface::AUTO && !interfaceHasIp(wanted)) {
        updateReachability(serverIdx, false);
        return false;
    }

    // 1. Resolve the name (blocking; works for WiFi and Ethernet alike).
    IPAddress ip;
    if (WiFi.hostByName(_servers[serverIdx].server, ip) != 1 || (uint32_t)ip == 0) {
        updateReachability(serverIdx, false);
        return false;
    }
    if (_abort) {
        return false;
    }

    // 2. Connected UDP socket: ephemeral local port, replies only from `ip`.
    if (!_udp.connect(ip, kPort)) {
        updateReachability(serverIdx, false);
        return false;
    }
    _udp.onPacket([this](AsyncUDPPacket packet) { this->onPacket(packet); });

    // 3. Arm the receive side, then send.
    const uint64_t t1 = Protocol::nowNtpUs();
    Packet request;
    Protocol::buildRequest(request, t1);
    uint8_t wire[kPacketSize];
    request.toBytes(wire);

    xSemaphoreTake(_rxSem, 0);                       // drop any stale token
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _txSec = request.txTm_s;
    _txFrac = request.txTm_f;
    _waiting = true;
    xSemaphoreGive(_mutex);

    size_t written;
    if (wanted == NTPNetworkInterface::AUTO) {
        written = _udp.write(wire, kPacketSize);                       // lwIP default route
    } else {
        written = _udp.writeTo(wire, kPacketSize, ip, kPort,           // forced interface
                               (wanted == NTPNetworkInterface::WIFI) ? TCPIP_ADAPTER_IF_STA
                                                                     : TCPIP_ADAPTER_IF_ETH);
    }
    const bool sent = (written == kPacketSize);

    // 4. Wait for the reply (in short slices so stop() can interrupt us).
    bool got = false;
    if (sent) {
        const uint32_t start = millis();
        while (!_abort) {
            const uint32_t elapsed = millis() - start;
            if (elapsed >= _timeout) {
                break;
            }
            uint32_t slice = _timeout - elapsed;
            if (slice > 50) {
                slice = 50;
            }
            if (xSemaphoreTake(_rxSem, pdMS_TO_TICKS(slice)) == pdTRUE) {
                got = true;
                break;
            }
        }
    }

    Packet reply;
    uint64_t t4 = 0;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _waiting = false;
    if (got) {
        reply = _rx;
        t4 = _rxT4;
    }
    xSemaphoreGive(_mutex);
    _udp.close();

    if (!got) {
        if (!_abort) {
            updateReachability(serverIdx, false);
        }
        return false;
    }

    // 5. Validate and turn the timestamps into offset and delay.
    double offset;
    float delay;
    if (!Protocol::parseResponse(reply, t1, t4, offset, delay)) {
        updateReachability(serverIdx, false);
        return false;
    }

    updateMetrics(serverIdx, offset, delay, reply.stratum, reply.rootDispersion);
    updateReachability(serverIdx, true);
    return true;
}

void ServerPool::updateMetrics(int serverIdx, double offset, float delay,
                               uint8_t stratum, uint32_t rootDisp) {
    NTPServerMetrics& srv = _servers[serverIdx];

    // Jitter: exponential average of how much the offset moved between syncs.
    if (srv.lastSync > 0) {
        const float diff = (float)fabs(offset - srv.offset);
        srv.jitter = srv.jitter * 0.875f + diff * 0.125f;
    } else {
        srv.jitter = 0.0f;
    }

    srv.offset = offset;
    srv.delay = delay;
    srv.stratum = stratum;
    srv.dispersion = Protocol::shortToMs(rootDisp);
    srv.lastSync = millis();
    srv.failCount = 0;
}

void ServerPool::updateReachability(int serverIdx, bool success) {
    NTPServerMetrics& srv = _servers[serverIdx];

    srv.reach = (uint8_t)((srv.reach << 1) | (success ? 1 : 0));
    srv.reachable = (srv.reach != 0);
    if (!success && srv.failCount < 255) {
        srv.failCount++;
    }
}

void ServerPool::selectBestServer() {
    _bestServerIdx = -1;
    float bestQuality = 999999.0f;

    for (int i = 0; i < _serverCount; i++) {
        // failCount == 0 means the latest query succeeded, so the stored
        // offset is fresh. A server that failed this round is never used,
        // otherwise its old offset would be applied a second time.
        if (!_servers[i].reachable || _servers[i].failCount != 0) {
            continue;
        }
        const float quality = _servers[i].quality();
        if (quality < bestQuality) {
            bestQuality = quality;
            _bestServerIdx = i;
        }
    }
}

const NTPServerMetrics* ServerPool::getMetrics(int& count) const {
    count = _serverCount;
    return _servers;
}

const NTPServerMetrics* ServerPool::getBestServer() const {
    if (_bestServerIdx >= 0 && _bestServerIdx < _serverCount) {
        return &_servers[_bestServerIdx];
    }
    return nullptr;
}

void ServerPool::applyCorrection(double appliedMs) {
    for (int i = 0; i < _serverCount; i++) {
        if (_servers[i].lastSync > 0) {
            _servers[i].offset -= appliedMs;
        }
    }
}

}  // namespace ntpca
