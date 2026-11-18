// ============================================================================
// BasicWiFi - sync the system clock over WiFi and print the time
// ============================================================================
#include <WiFi.h>
#include <NTPClientAdvanced.h>

const char* WIFI_SSID = "YOUR_SSID";
const char* WIFI_PASS = "YOUR_PASSWORD";

NTPClientAdvanced ntpClient;

void setup() {
    Serial.begin(115200);

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected");

    ntpClient.setNetworkInterface(NTPNetworkInterface::WIFI);
    ntpClient.setServers("pool.ntp.org", "time.google.com", "time.cloudflare.com");
    ntpClient.setInterval(3600);       // sync every hour
    ntpClient.setTimeZone(3, 30);      // UTC+3:30 (Tehran)
    ntpClient.setTimeout(2000);        // wait up to 2 s per server

    // Callbacks run on the NTP task, so keep them short.
    ntpClient.onSyncEvent([](bool success) {
        Serial.printf("NTP sync %s\n", success ? "OK" : "failed");
    });

    ntpClient.onMetricsUpdate([](const NTPServerMetrics* metrics, int count) {
        for (int i = 0; i < count; i++) {
            const NTPServerMetrics& m = metrics[i];
            if (m.reachable) {
                Serial.printf("  %-22s stratum %u  delay %.1f ms  jitter %.2f ms  quality %.1f\n",
                              m.server, m.stratum, m.delay, m.jitter, m.quality());
            } else {
                Serial.printf("  %-22s unreachable\n", m.server);
            }
        }
    });

    if (!ntpClient.begin()) {
        Serial.println("Failed to start NTP client");
    }
}

void loop() {
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint >= 10000) {
        lastPrint = millis();
        if (ntpClient.isSynced()) {
            Serial.printf("Time: %s  (last sync %lu s ago)\n",
                          ntpClient.getTimeString().c_str(),
                          (unsigned long)((millis() - ntpClient.getLastSync()) / 1000));
        } else {
            Serial.println("Waiting for time sync...");
        }
    }
    delay(100);
}
