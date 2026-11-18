// ============================================================================
// ManualUpdate - trigger a sync from the serial monitor
//   u = force an update now    s = show status    h = help
// ============================================================================
#include <WiFi.h>
#include <NTPClientAdvanced.h>

const char* WIFI_SSID = "YOUR_SSID";
const char* WIFI_PASS = "YOUR_PASSWORD";

NTPClientAdvanced ntpClient;

void printStatus() {
    Serial.println("\n=== NTP status ===");
    Serial.printf("Synced : %s\n", ntpClient.isSynced() ? "yes" : "no");
    Serial.printf("Time   : %s\n", ntpClient.getTimeString().c_str());
    Serial.printf("Epoch  : %lu (timezone applied)\n", (unsigned long)ntpClient.getEpochTime());
    Serial.printf("Last clock step: %.2f ms\n", ntpClient.getLastAdjustmentMs());

    int count = 0;
    ntpClient.getMetrics(count);
    Serial.printf("Servers: %d\n", count);

    const NTPServerMetrics* best = ntpClient.getBestServer();
    if (best) {
        Serial.printf("Best   : %s (delay %.1f ms)\n", best->server, best->delay);
    }
}

void setup() {
    Serial.begin(115200);

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }

    ntpClient.setServers("pool.ntp.org", "time.google.com");
    ntpClient.setInterval(7200);       // every 2 hours unless forced
    ntpClient.setTimeZone(3, 30);

    ntpClient.onSyncEvent([](bool success) {
        Serial.printf("Sync: %s\n", success ? "OK" : "FAIL");
    });

    ntpClient.begin();
    Serial.println("Commands: u = force update, s = status, h = help");
}

void loop() {
    if (Serial.available()) {
        char cmd = Serial.read();
        if (cmd == 'u' || cmd == 'U') {
            Serial.println("Forcing NTP update...");
            ntpClient.forceUpdate();
        } else if (cmd == 's' || cmd == 'S') {
            printStatus();
        } else if (cmd == 'h' || cmd == 'H') {
            Serial.println("u = force update\ns = status\nh = help");
        }
    }
    delay(50);
}
