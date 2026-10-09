#include "wifi.hpp"
#include "settings.hpp"

// =============================================================================
// VARIABLES STATIQUES
// =============================================================================

static bool g_wifi_initialized = false;

// =============================================================================
// IMPLEMENTATION
// =============================================================================

bool wifiConnect(uint16_t timeout_sec) {
    if (g_wifi_initialized && WiFi.status() == WL_CONNECTED) {
        return true;
    }
    
    Serial.println("Connecting to WiFi: " + String(settingsGet().wifi_ssid));

    // Réinitialise complètement la radio après le deep sleep : esp_wifi_stop()
    // laisse parfois la pile WiFi dans un état où le scan échoue
    // (WIFI_SCAN_FAILED, code -2) et la connexion n'aboutit pas.
    // disconnect(true) ne coûte rien ici : la radio est déjà éteinte.
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);

    // Configuration pour économie d'énergie
    // Identité réseau AVANT mode(WIFI_STA) : le core applique le hostname au
    // netif STA dans mode() (sinon il envoie son défaut "esp32-XXXXXX").
    WiFi.setHostname(WIFI_HOSTNAME);
    WiFi.mode(WIFI_STA);
    delay(100); // laisse la radio s'initialiser avant begin()

    // PAS de reconnexion auto : on se reconnecte explicitement à chaque réveil.
    // setAutoReconnect(true) fait boucler la pile WiFi en retries internes quand
    // l'AP est absent → le timeout n'est plus respecté et ça consomme ~150 mA
    // pendant des dizaines de secondes.
    WiFi.setAutoConnect(false);
    WiFi.setAutoReconnect(false);
    WiFi.setScanMethod(WIFI_FAST_SCAN);

    // Connexion directe au SSID, sans scan préalable
    WiFi.begin(settingsGet().wifi_ssid, settingsGet().wifi_pass);

    // Attend la connexion avec timeout
    uint32_t start_time = millis();
    uint8_t status = WiFi.status();

    while (status != WL_CONNECTED &&
           (millis() - start_time) < (timeout_sec * 1000)) {
        delay(100);
        status = WiFi.status();
        Serial.print(".");
    }

    if (status == WL_CONNECTED) {
        g_wifi_initialized = true;
        Serial.println("");
        Serial.println("WiFi connected!");
        Serial.println("IP: " + WiFi.localIP().toString());
        return true;
    } else {
        Serial.println("");
        Serial.printf("WiFi connection FAILED! (status=%d)\n", status);
        return false;
    }
}

void wifiDisconnect() {
    if (!g_wifi_initialized) return;
    
    Serial.println("Disconnecting WiFi...");
    
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    
    #ifdef ESP32
    // Pour l'ESP32, on peut aussi arrêter le controller Bluetooth
    // qui partage le WiFi
    #endif
    
    g_wifi_initialized = false;
    Serial.println("WiFi disconnected and powered off.");
}

bool wifiIsConnected() {
    return (WiFi.status() == WL_CONNECTED);
}

int16_t wifiRSSI() {
    if (!g_wifi_initialized || !wifiIsConnected()) return 0;
    return (int16_t)WiFi.RSSI();
}
