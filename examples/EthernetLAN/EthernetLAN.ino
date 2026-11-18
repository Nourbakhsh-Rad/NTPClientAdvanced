// ============================================================================
// EthernetLAN - sync the system clock over Ethernet
//
// ETH.begin() without arguments only works on boards whose variant defines the
// PHY pins. Otherwise pass the PHY type/address/pins for your board, e.g. on
// core 2.x: ETH.begin(ETH_PHY_ADDR, ETH_PHY_POWER, ETH_PHY_MDC, ETH_PHY_MDIO,
//                     ETH_PHY_TYPE, ETH_CLK_MODE);
// ============================================================================
#include <ETH.h>
#include <WiFi.h>
#include <NTPClientAdvanced.h>

NTPClientAdvanced ntpClient;

void onNetworkEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_ETH_START:
            ETH.setHostname("esp32-ethernet");
            break;
        case ARDUINO_EVENT_ETH_GOT_IP:
            Serial.print("ETH IP: ");
            Serial.println(ETH.localIP());
            // Start NTP once we have an address. A second GOT_IP after a cable
            // re-plug makes begin() return false, which is harmless: the
            // running client simply retries on its own.
            ntpClient.begin();
            break;
        case ARDUINO_EVENT_ETH_DISCONNECTED:
            Serial.println("ETH disconnected");
            break;
        default:
            break;
    }
}

void setup() {
    Serial.begin(115200);
    WiFi.onEvent(onNetworkEvent);

    ntpClient.setNetworkInterface(NTPNetworkInterface::ETHERNET);
    ntpClient.setServers("pool.ntp.org", "time.google.com", "time.cloudflare.com");
    ntpClient.setInterval(1800);       // every 30 minutes
    ntpClient.setTimeZone(3, 30);

    ntpClient.onSyncEvent([](bool success) {
        Serial.printf("[ETH] NTP sync %s, time %s\n",
                      success ? "OK" : "failed", ntpClient.getTimeString().c_str());
    });

    ETH.begin();
}

void loop() {
    delay(10000);
}
