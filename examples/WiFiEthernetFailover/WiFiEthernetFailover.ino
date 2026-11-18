// ============================================================================
// WiFiEthernetFailover - WiFi and Ethernet both enabled
//
// With the default NTPNetworkInterface::AUTO, lwIP's default route decides which
// interface carries the NTP traffic, and the client needs no restart when the
// active link changes: every sync opens a new UDP socket, and a failed sync is
// retried automatically (5 s, then 10 s, 20 s ... up to 5 minutes).
//
// If both links are up and you want one of them to be used, call
//   ntpClient.setNetworkInterface(NTPNetworkInterface::ETHERNET);   // or WIFI
// The client then never falls back to the other interface.
// ============================================================================
#include <WiFi.h>
#include <ETH.h>
#include <NTPClientAdvanced.h>

const char* WIFI_SSID = "YOUR_SSID";
const char* WIFI_PASS = "YOUR_PASSWORD";

NTPClientAdvanced ntpClient;

const char* activeLink() {
    if (ETH.linkUp()) return "ETH";
    if (WiFi.status() == WL_CONNECTED) return "WiFi";
    return "none";
}

void setup() {
    Serial.begin(115200);

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    ETH.begin();                        // see EthernetLAN example for board-specific pins

    // ntpClient.setNetworkInterface(NTPNetworkInterface::ETHERNET);   // optional: force Ethernet
    ntpClient.setServers("pool.ntp.org", "time.google.com", "time.cloudflare.com");
    ntpClient.setInterval(3600);
    ntpClient.setTimeZone(3, 30);

    ntpClient.onSyncEvent([](bool success) {
        Serial.printf("[%s] NTP sync %s\n", activeLink(), success ? "OK" : "failed");
    });

    // Start right away; until a link is up the client just keeps retrying.
    ntpClient.begin();
}

void loop() {
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint >= 10000) {
        lastPrint = millis();
        Serial.printf("[%s] %s\n", activeLink(),
                      ntpClient.isSynced() ? ntpClient.getTimeString().c_str() : "not synced yet");
    }
    delay(100);
}
