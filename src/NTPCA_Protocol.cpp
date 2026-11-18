// ============================================================================
// NTPCA_Protocol.cpp
// ============================================================================
#include "NTPCA_Protocol.h"

#include <math.h>
#include <string.h>
#include <sys/time.h>

namespace ntpca {

namespace {

const int64_t kEraUs = 4294967296000000LL;  // 2^32 seconds in microseconds
const int64_t kHalfEraUs = kEraUs / 2;

// a - b for two timestamps that are only known modulo one NTP era. The result
// is the representative in (-68 years, +68 years], which is what RFC 5905
// (section 6) prescribes. This keeps the maths right across the 2036 rollover
// and when the local clock still sits at 1970 after a cold boot.
inline int64_t eraDiff(int64_t a, int64_t b) {
    int64_t d = a - b;
    if (d > kHalfEraUs) {
        d -= kEraUs;
    } else if (d <= -kHalfEraUs) {
        d += kEraUs;
    }
    return d;
}

inline uint32_t getBe32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

inline void putBe32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

}  // namespace

// ---------------------------------------------------------------------------
// Packet
// ---------------------------------------------------------------------------
void Packet::clear() {
    memset(this, 0, sizeof(Packet));
}

void Packet::toBytes(uint8_t* out) const {
    out[0] = liVnMode;
    out[1] = stratum;
    out[2] = poll;
    out[3] = precision;
    putBe32(out + 4, rootDelay);
    putBe32(out + 8, rootDispersion);
    putBe32(out + 12, refId);
    putBe32(out + 16, refTm_s);
    putBe32(out + 20, refTm_f);
    putBe32(out + 24, origTm_s);
    putBe32(out + 28, origTm_f);
    putBe32(out + 32, rxTm_s);
    putBe32(out + 36, rxTm_f);
    putBe32(out + 40, txTm_s);
    putBe32(out + 44, txTm_f);
}

void Packet::fromBytes(const uint8_t* in) {
    liVnMode = in[0];
    stratum = in[1];
    poll = in[2];
    precision = in[3];
    rootDelay = getBe32(in + 4);
    rootDispersion = getBe32(in + 8);
    refId = getBe32(in + 12);
    refTm_s = getBe32(in + 16);
    refTm_f = getBe32(in + 20);
    origTm_s = getBe32(in + 24);
    origTm_f = getBe32(in + 28);
    rxTm_s = getBe32(in + 32);
    rxTm_f = getBe32(in + 36);
    txTm_s = getBe32(in + 40);
    txTm_f = getBe32(in + 44);
}

// ---------------------------------------------------------------------------
// Time format helpers
// ---------------------------------------------------------------------------
void Protocol::usToNtp(uint64_t us, uint32_t& sec, uint32_t& frac) {
    us %= (uint64_t)kEraUs;
    sec = (uint32_t)(us / 1000000ULL);
    frac = (uint32_t)(((us % 1000000ULL) << 32) / 1000000ULL);
}

uint64_t Protocol::ntpToUs(uint32_t sec, uint32_t frac) {
    return (uint64_t)sec * 1000000ULL + (((uint64_t)frac * 1000000ULL) >> 32);
}

uint64_t Protocol::nowNtpUs() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return ((uint64_t)tv.tv_sec + kUnixEpochOffset) * 1000000ULL + (uint64_t)tv.tv_usec;
}

float Protocol::shortToMs(uint32_t value) {
    uint32_t seconds = (value >> 16) & 0xFFFF;
    uint32_t fraction = value & 0xFFFF;
    return (float)seconds * 1000.0f + (float)fraction * 1000.0f / 65536.0f;
}

// ---------------------------------------------------------------------------
// Request / response
// ---------------------------------------------------------------------------
void Protocol::buildRequest(Packet& packet, uint64_t t1Us) {
    packet.clear();
    packet.liVnMode = (0 << 6) | (4 << 3) | 3;   // LI 0, NTPv4, mode 3 (client)
    usToNtp(t1Us, packet.txTm_s, packet.txTm_f);
}

bool Protocol::validate(const Packet& packet) {
    if (packet.mode() != 4) {                    // server reply
        return false;
    }
    if (packet.leap() == 3) {                    // server clock unsynchronised
        return false;
    }
    if (packet.version() < 3) {
        return false;
    }
    if (packet.stratum == 0 || packet.stratum > 15) {   // 0 = kiss-o'-death
        return false;
    }
    if (packet.txTm_s == 0 && packet.txTm_f == 0) {   // zero = not set
        return false;
    }
    return true;
}

bool Protocol::parseResponse(const Packet& packet, uint64_t t1Us, uint64_t t4Us,
                             double& offsetMs, float& delayMs) {
    if (!validate(packet) || (packet.rxTm_s == 0 && packet.rxTm_f == 0)) {
        return false;
    }

    const int64_t t1 = (int64_t)(t1Us % (uint64_t)kEraUs);
    const int64_t t4 = (int64_t)(t4Us % (uint64_t)kEraUs);
    const int64_t t2 = (int64_t)ntpToUs(packet.rxTm_s, packet.rxTm_f);
    const int64_t t3 = (int64_t)ntpToUs(packet.txTm_s, packet.txTm_f);

    // Time the server spent on the request (t3 - t2) must not be negative.
    const int64_t serverBusy = eraDiff(t3, t2);
    if (serverBusy < 0) {
        return false;
    }

    // delay = (t4 - t1) - (t3 - t2)
    int64_t delayUs = eraDiff(t4, t1) - serverBusy;
    if (delayUs < 0) {
        if (delayUs < -5000) {                   // clearly inconsistent
            return false;
        }
        delayUs = 0;                             // timestamp rounding
    }
    if (delayUs > (int64_t)NTPCA_MAX_DELAY_MS * 1000) {
        return false;
    }

    // offset = ((t2 - t1) + (t3 - t4)) / 2
    const int64_t twiceOffsetUs = eraDiff(t2, t1) + eraDiff(t3, t4);
    offsetMs = (double)twiceOffsetUs / 2000.0;
    delayMs = (float)delayUs / 1000.0f;
    return true;
}

bool Protocol::stepClock(double offsetMs) {
    struct timeval tv;
    if (gettimeofday(&tv, nullptr) != 0) {
        return false;
    }
    int64_t us = (int64_t)tv.tv_sec * 1000000LL + (int64_t)tv.tv_usec + llround(offsetMs * 1000.0);
    if (us < 0) {
        return false;
    }
    tv.tv_sec = (time_t)(us / 1000000LL);
    tv.tv_usec = (suseconds_t)(us % 1000000LL);
    return settimeofday(&tv, nullptr) == 0;
}

}  // namespace ntpca
