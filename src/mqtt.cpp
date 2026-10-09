#include "mqtt.hpp"
#include "wifi.hpp"
#include "settings.hpp"

#include <PubSubClient.h>
#include <WiFiClient.h>
#include <math.h>

// =============================================================================
// CLIENT MQTT
// =============================================================================

static WiFiClient mqtt_wifi_client;
static PubSubClient mqtt_client(mqtt_wifi_client);

// Suffixes des topics (la base vient de la NVS, saisie via le portail).
static const char* const kSuffixValue       = "/value";
static const char* const kSuffixBattery     = "/battery";
static const char* const kSuffixVersion     = "/version";
static const char* const kSuffixError       = "/error/on-off";
static const char* const kSuffixQuality     = "/quality";
static const char* const kSuffixRefillState = "/refill/state";
static const char* const kSuffixRefillEvent = "/refill/event";

// Construit "<base>/<suffix>" dans un buffer local (la base vit en NVS).
static void buildTopic(char* out, size_t len, const char* suffix) {
    snprintf(out, len, "%s%s", settingsGet().mqtt_base, suffix);
}

// Déclaration anticipée : mqttConnect() publie la version firmware dès la
// connexion (le helper est défini plus bas).
static bool mqttPublish(const char* topic, const char* payload, bool retain);

// =============================================================================
// IMPLEMENTATION
// =============================================================================

// Identifiant client unique dérivé de l'adresse MAC (buffer fixe, pas de heap).
static void mqttMakeClientId(char* out, size_t len) {
    uint64_t mac = ESP.getEfuseMac();
    snprintf(out, len, "espscale-%04X%08X",
             (uint16_t)(mac >> 32), (uint32_t)mac);
}

bool mqttConnect(uint16_t timeout_sec) {
    if (!wifiIsConnected()) {
        Serial.println("MQTT connect: WiFi not connected!");
        return false;
    }

    mqtt_client.setServer(settingsGet().mqtt_server, settingsGet().mqtt_port);

    char client_id[32];
    mqttMakeClientId(client_id, sizeof(client_id));
    Serial.printf("MQTT connecting to %s:%u as %s...\n",
                  settingsGet().mqtt_server, settingsGet().mqtt_port, client_id);

    // Authentification optionnelle : elle n'est envoyée que si un utilisateur
    // est configuré (vide = broker sans auth, cas par défaut).
    const bool use_auth = (settingsGet().mqtt_user[0] != '\0');

    // Deadline GLOBALE (pas un timeout par lecture) : on borne la boucle de
    // connexion sur millis(), comme pour le WiFi.
    uint32_t deadline = millis() + (uint32_t)timeout_sec * 1000UL;
    bool connected = false;
    while (!connected && (int32_t)(deadline - millis()) > 0) {
        connected = use_auth
            ? mqtt_client.connect(client_id, settingsGet().mqtt_user,
                                  settingsGet().mqtt_pass)
            : mqtt_client.connect(client_id);
        if (!connected) delay(250);
    }

    if (!connected) {
        Serial.printf("MQTT connection FAILED (state=%d)\n", mqtt_client.state());
        return false;
    }

    Serial.println("MQTT connected!");

    // Pas d'algorithme de Nagle : sinon les petits messages publiés à la suite
    // du premier attendent son ACK dans la pile TCP.
    mqtt_wifi_client.setNoDelay(true);

    // Publie la version firmware (retain) à chaque connexion : le broker garde
    // ainsi toujours la version réellement flashée, sans dépendre du cycle de
    // publication du poids.
    mqttPublishVersion();
    return true;
}

bool mqttTestConnection(const char* server, uint16_t port,
                        const char* user, const char* pass,
                        uint16_t timeout_sec) {
    if (!wifiIsConnected()) {
        Serial.println("[mqtt-test] WiFi non connecte");
        return false;
    }

    // PubSubClient::setServer() NE COPIE PAS la chaîne (il garde le pointeur) :
    // on l'héberge donc dans un buffer statique, sinon le client garderait un
    // pointeur sur un argument local une fois la fonction retournée.
    static char test_server[40];
    snprintf(test_server, sizeof(test_server), "%s", server ? server : "");
    mqtt_client.setServer(test_server, port);

    char client_id[32];
    mqttMakeClientId(client_id, sizeof(client_id));
    const bool use_auth = (user != nullptr && user[0] != '\0');
    Serial.printf("[mqtt-test] %s:%u%s...\n", server, port,
                  use_auth ? " (avec auth)" : " (sans auth)");

    uint32_t deadline = millis() + (uint32_t)timeout_sec * 1000UL;
    bool connected = false;
    while (!connected && (int32_t)(deadline - millis()) > 0) {
        connected = use_auth ? mqtt_client.connect(client_id, user, pass)
                             : mqtt_client.connect(client_id);
        if (!connected) delay(250);
    }
    Serial.printf("[mqtt-test] %s (state=%d)\n",
                  connected ? "OK" : "ECHEC", mqtt_client.state());
    if (connected) mqtt_client.disconnect();
    return connected;
}

/**
 * @brief Helper de publication unique (retire la duplication des 6 fonctions).
 * @return true si le client était connecté et le publish accepté.
 */
static bool mqttPublish(const char* topic, const char* payload, bool retain) {
    if (!mqtt_client.connected()) {
        Serial.println("Cannot publish: MQTT not connected");
        return false;
    }

    bool ok = mqtt_client.publish(topic, payload, retain);
    if (ok) {
        Serial.printf("Published '%s' -> %s\n", payload, topic);
    } else {
        Serial.printf("MQTT publish FAILED '%s' (state=%d)\n", topic, mqtt_client.state());
    }
    return ok;
}

// --- Publications métier (toutes via le helper) ----------------------------

// Assez grand pour la base (23 max) + le plus long suffixe ("/error/on-off").
static const size_t kTopicMax = 48;

bool mqttPublishWeight(float weight) {
    char topic[kTopicMax];
    char payload[8];
    buildTopic(topic, sizeof(topic), kSuffixValue);
    // Poids publié en ENTIER (arrondi) — le stock HA et l'écran partagent la
    // même valeur entière (règle projet : pas de décimales sur le poids).
    snprintf(payload, sizeof(payload), "%u", (unsigned)lroundf(weight));
    return mqttPublish(topic, payload, true);
}

bool mqttPublishBattery(uint8_t percent) {
    char topic[kTopicMax];
    char payload[8];
    buildTopic(topic, sizeof(topic), kSuffixBattery);
    snprintf(payload, sizeof(payload), "%u", percent);
    return mqttPublish(topic, payload, true);
}

bool mqttPublishVersion() {
    char topic[kTopicMax];
    buildTopic(topic, sizeof(topic), kSuffixVersion);
    // Version statique du firmware (FW_VERSION) — retain pour qu'elle survive
    // au redémarrage du broker.
    return mqttPublish(topic, FW_VERSION, true);
}

bool mqttPublishError(bool error) {
    char topic[kTopicMax];
    buildTopic(topic, sizeof(topic), kSuffixError);
    return mqttPublish(topic, error ? "on" : "off", true);
}

bool mqttPublishQuality(uint8_t valid_count, const char* mode) {
    char topic[kTopicMax];
    char payload[64];
    buildTopic(topic, sizeof(topic), kSuffixQuality);
    snprintf(payload, sizeof(payload), "{\"valid\":%u,\"mode\":\"%s\"}", valid_count, mode);
    return mqttPublish(topic, payload, true);
}

bool mqttPublishRefillState(const char* state) {
    char topic[kTopicMax];
    buildTopic(topic, sizeof(topic), kSuffixRefillState);
    return mqttPublish(topic, state, true);
}

bool mqttPublishRefillEvent(float added_kg, float bag_price_eur, uint8_t bag_count) {
    char topic[kTopicMax];
    char payload[128];
    buildTopic(topic, sizeof(topic), kSuffixRefillEvent);
    // Le coût total = bag_count × bag_price_eur (calculé côté HA).
    snprintf(payload, sizeof(payload),
             "{\"added_kg\":%.1f,\"bag_price_eur\":%.2f,\"bag_count\":%u}",
             added_kg, bag_price_eur, bag_count);
    // Pas de retain : c'est un événement ponctuel (source de vérité comptable).
    return mqttPublish(topic, payload, false);
}

void mqttDisconnect() {
    if (mqtt_client.connected()) {
        // Laisse partir les publications en attente AVANT la fermeture : le
        // WiFi est coupé aussitôt après, et ce qui n'a pas quitté la pile TCP
        // est perdu (mesuré : seul le 1er message d'une rafale arrivait).
        mqtt_client.loop();
        delay(150);
        mqtt_client.disconnect();
        Serial.println("MQTT disconnected.");
    }
}
