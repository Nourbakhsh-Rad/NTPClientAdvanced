// ============================================================================
// NTPCA_Protocol.h - NTP packet format, offset/delay maths, clock stepping
// ============================================================================
#pragma once

#include "NTPCA_Config.h"

namespace ntpca {

// NTPv4 packet (RFC 5905, figure 8). Serialised field by field in network
// byte order, so there is no packed struct and no dependency on htonl().
struct Packet {
    uint8_t liVnMode;
    uint8_t stratum;
    uint8_t poll;
    uint8_t precision;
    uint32_t rootDelay;
    uint32_t rootDispersion;
    uint32_t refId;
    uint32_t refTm_s, refTm_f;
    uint32_t origTm_s, origTm_f;
    uint32_t rxTm_s, rxTm_f;
    uint32_t txTm_s, txTm_f;

    uint8_t leap() const { return (liVnMode >> 6) & 0x03; }
    uint8_t version() const { return (liVnMode >> 3) & 0x07; }
    uint8_t mode() const { return liVnMode & 0x07; }

    void clear();
    void toBytes(uint8_t* out) const;        // writes kPacketSize bytes
    void fromBytes(const uint8_t* in);       // reads kPacketSize bytes
};

class Protocol {
public:
    // Fill a client request whose transmit timestamp is t1Us.
    static void buildRequest(Packet& packet, uint64_t t1Us);

    // Basic checks on a server reply (mode, leap, stratum, timestamps).
    static bool validate(const Packet& packet);

    // RFC 5905 offset/delay from t1 (request sent), the packet (t2, t3) and
    // t4 (reply received). All times are NTP microseconds. offset > 0 means
    // the local clock is behind the server.
    static bool parseResponse(const Packet& packet, uint64_t t1Us, uint64_t t4Us,
                              double& offsetMs, float& delayMs);

    // Current system time as microseconds since 1900-01-01.
    static uint64_t nowNtpUs();

    // Root dispersion/delay (NTP short format, 16.16 seconds) to milliseconds.
    static float shortToMs(uint32_t value);

    // Add offsetMs to the system clock (settimeofday).
    static bool stepClock(double offsetMs);

    // 64-bit NTP timestamp <-> microseconds (modulo one NTP era, 2^32 s).
    static void usToNtp(uint64_t us, uint32_t& sec, uint32_t& frac);
    static uint64_t ntpToUs(uint32_t sec, uint32_t frac);
};

}  // namespace ntpca
