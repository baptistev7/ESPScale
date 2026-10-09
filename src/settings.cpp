#include "settings.hpp"

#include "config.hpp"

#include <Preferences.h>
#include <cstring>

// =============================================================================
// IMPLEMENTATION
// =============================================================================

static const char* kNvsNamespace = "scale";

static Settings g_settings;
static SiloConfig g_silo;
static Calibration g_cal;
static bool g_loaded = false;
static bool g_stored = false; // une config a-t-elle été enregistrée en NVS ?

// Calibration par défaut = constantes compilées (config.hpp). Utilisées tant
// qu'aucune calibration n'a été enregistrée via le portail.
static void applyCalDefaults(Calibration& cal) {
    cal.z[0] = Z_FACTOR_1; cal.c[0] = C_FACTOR_1;
    cal.z[1] = Z_FACTOR_2; cal.c[1] = C_FACTOR_2;
    cal.z[2] = Z_FACTOR_3; cal.c[2] = C_FACTOR_3;
    cal.z[3] = Z_FACTOR_4; cal.c[3] = C_FACTOR_4;
}

// Défauts du silo = constantes compilées. Le seuil « niveau bas » est défini en
// POURCENTAGE dans config.hpp (SILO_LOW_ALERT_PCT) et stocké en kg : sa valeur
// par défaut est donc recalculée à partir de la capacité pour que le comportement
// d'aujourd'hui soit reproduit à l'identique.
static void applySiloDefaults(SiloConfig& silo) {
    silo.capacity_kg = TANK_FULL_KG;
    silo.low_kg      = TANK_FULL_KG * (float)SILO_LOW_ALERT_PCT / 100.0f;
    silo.bag_kg      = BAG_KG;
}

// Heure d'aspiration par défaut : 18h30, l'horaire pour lequel ce firmware a été
// écrit (réveils à 18h00 et 19h00, soit ±30 min). Une carte sans réglage est donc
// une carte qui se comporte EXACTEMENT comme avant.
constexpr uint16_t kDefaultAspirationMin = 18 * 60 + 30;

// Un réglage du silo hors bornes rendrait l'affichage faux (taux > 100 %,
// seuil au-dessus de la capacité) : on borne au chargement comme à l'écriture.
static bool siloIsSane(const SiloConfig& s) {
    return s.capacity_kg > 0.0f && s.capacity_kg <= 5000.0f &&
           s.low_kg >= 0.0f && s.low_kg <= s.capacity_kg &&
           s.bag_kg >= 1.0f && s.bag_kg <= 50.0f;
}

// Copie une chaîne en bornant la longueur (garde toujours le NUL final).
static void copyStr(char* dst, size_t len, const char* src) {
    if (len == 0) return;
    strncpy(dst, src ? src : "", len - 1);
    dst[len - 1] = '\0';
}

void settingsNormalize(Settings& s) {
    // Hors 0..1439, l'ordonnanceur calculerait des créneaux aberrants (un
    // réveil à 25h) : on ramène dans la journée.
    if (s.aspiration_min >= 1440) s.aspiration_min = kDefaultAspirationMin;

    // Schéma éventuel ("http://", "mqtt://"…) : PubSubClient attend un hôte seul.
    static const char* const kSchemes[] = {
        "http://", "https://", "mqtt://", "mqtts://", "tcp://", "ssl://"
    };
    for (const char* sc : kSchemes) {
        const size_t n = strlen(sc);
        if (strncmp(s.mqtt_server, sc, n) == 0) {
            memmove(s.mqtt_server, s.mqtt_server + n,
                    strlen(s.mqtt_server + n) + 1);
            break;
        }
    }
    // Chemin éventuel ("hote/valeur").
    if (char* slash = strchr(s.mqtt_server, '/')) *slash = '\0';
    // Port collé au serveur ("hote:1883") → champ port.
    if (char* colon = strchr(s.mqtt_server, ':')) {
        long p = atol(colon + 1);
        if (p >= 1 && p <= 65535) {
            *colon = '\0';
            s.mqtt_port = (uint16_t)p;
        }
    }
}

// Aucune valeur propre au site n'est compilée : WiFi et serveur MQTT viennent
// uniquement de la NVS (portail). Seuls deux DÉFAUTS de formulaire sont posés
// ici, car ils ne dépendent pas du site et restent modifiables dans le portail :
// le port MQTT (port standard du protocole) et la base des topics ("scale").
static void applyDefaults(Settings& s) {
    s.wifi_ssid[0]   = '\0';
    s.wifi_pass[0]   = '\0';
    s.mqtt_server[0] = '\0';
    s.mqtt_user[0]   = '\0';
    s.mqtt_pass[0]   = '\0';
    s.mqtt_port      = MQTT_PORT;
    s.aspiration_min = kDefaultAspirationMin;
    copyStr(s.mqtt_base, sizeof(s.mqtt_base), MQTT_TOPIC_BASE_DEFAULT);
}

void settingsLoad() {
    applyDefaults(g_settings);
    applyCalDefaults(g_cal);
    g_stored = false;

    Preferences prefs;
    // Ouverture en lecture/écriture : sur une carte neuve le namespace n'existe
    // pas encore, et une ouverture en lecture seule échouerait avec un message
    // d'erreur trompeur dans les logs. Aucune écriture n'est faite ici.
    if (!prefs.begin(kNvsNamespace, /*readOnly=*/false)) {
        g_loaded = true;
        return;
    }

    // Une config est « enregistrée » quand le RÉSEAU ET le BROKER le sont
    // tous les deux : ce sont les deux seules choses sans quoi la carte ne peut
    // rien publier. Tout le reste (calibration des pieds, base des topics,
    // identifiants) a des valeurs par défaut et ne doit donc pas envoyer au
    // portail — c'est ce qui décide du démarrage dans le portail.
    g_stored = prefs.isKey("wifi_ssid") && prefs.isKey("mqtt_server");

    // Lecture clé par clé, protégée par isKey() : lire une clé absente fait
    // loguer un `NOT_FOUND` par Preferences (bruit trompeur sur carte neuve).
    auto loadStr = [&prefs](const char* key, char* dst, size_t len) {
        if (!prefs.isKey(key)) return;
        String s = prefs.getString(key, "");
        if (s.length()) copyStr(dst, len, s.c_str());
    };

    loadStr("wifi_ssid", g_settings.wifi_ssid, sizeof(g_settings.wifi_ssid));
    loadStr("wifi_pass", g_settings.wifi_pass, sizeof(g_settings.wifi_pass));
    loadStr("mqtt_server", g_settings.mqtt_server, sizeof(g_settings.mqtt_server));
    loadStr("mqtt_base", g_settings.mqtt_base, sizeof(g_settings.mqtt_base));
    loadStr("mqtt_user", g_settings.mqtt_user, sizeof(g_settings.mqtt_user));
    loadStr("mqtt_pass", g_settings.mqtt_pass, sizeof(g_settings.mqtt_pass));

    if (prefs.isKey("mqtt_port")) {
        uint16_t port = prefs.getUShort("mqtt_port", 0);
        if (port != 0) g_settings.mqtt_port = port;
    }

    // Heure d'aspiration :minutes depuis minuit. Absente = défaut 18h30, donc une
    // carte déjà en service garde EXACTEMENT les réveils d'aujourd'hui (18h/19h).
    // Hors bornes (>= 1440) = NVS corrompue, on retombe sur le défaut.
    if (prefs.isKey("aspiration_min")) {
        uint16_t m = prefs.getUShort("aspiration_min", kDefaultAspirationMin);
        if (m < 1440) g_settings.aspiration_min = m;
    }

    // Calibration : clés propres (cal_z0..3 / cal_c0..3). Absentes = on garde
    // les défauts compilés. Un facteur d'échelle nul serait inutilisable (division
    // par zéro côté HX711) → on l'ignore et on conserve le défaut.
    static const char* const z_keys[4] = {"cal_z0", "cal_z1", "cal_z2", "cal_z3"};
    static const char* const c_keys[4] = {"cal_c0", "cal_c1", "cal_c2", "cal_c3"};
    for (uint8_t i = 0; i < 4; i++) {
        if (prefs.isKey(z_keys[i])) g_cal.z[i] = prefs.getFloat(z_keys[i], g_cal.z[i]);
        if (prefs.isKey(c_keys[i])) {
            float c = prefs.getFloat(c_keys[i], g_cal.c[i]);
            if (c != 0.0f) g_cal.c[i] = c;
        }
    }

    // Réglages du silo : clés propres (silo_capacity / silo_low / silo_bag_kg),
    // absentes = on garde exactement les constantes compilées. Hors bornes, on
    // retombe aussi sur les défauts (une écriture tronquée ne doit pas casser
    // l'affichage).
    //
    // L'ancienne « tare silo vide » (silo_tare) est supprimée : elle faisait
    // doublon avec le zéro de la calibration (tare des 4 pieds). La clé est
    // effacée ; si elle valait > 0, le poids publié remonte d'autant une fois.
    if (prefs.isKey("silo_tare")) {
        Serial.printf("[settings] tare silo %.1f kg supprimee (doublon calibration)\n",
                      prefs.getFloat("silo_tare", 0.0f));
        prefs.remove("silo_tare");
    }
    applySiloDefaults(g_silo);
    {
        SiloConfig stored = g_silo;
        if (prefs.isKey("silo_capacity")) stored.capacity_kg = prefs.getFloat("silo_capacity", stored.capacity_kg);
        if (prefs.isKey("silo_low"))      stored.low_kg      = prefs.getFloat("silo_low", stored.low_kg);
        if (prefs.isKey("silo_bag_kg"))   stored.bag_kg      = prefs.getFloat("silo_bag_kg", stored.bag_kg);
        if (siloIsSane(stored)) {
            g_silo = stored;
        } else if (prefs.isKey("silo_capacity") || prefs.isKey("silo_low") ||
                   prefs.isKey("silo_bag_kg")) {
            Serial.println("[settings] silo config hors bornes -> defauts");
        }
    }

    // Nettoie les valeurs héritées (ex. serveur enregistré "http://192.168.1.2"
    // avant que le champ ne soit nettoyé) : la config redevient utilisable sans
    // avoir à la ressaisir.
    settingsNormalize(g_settings);

    prefs.end();
    g_loaded = true;
}

const Settings& settingsGet() {
    if (!g_loaded) settingsLoad();
    return g_settings;
}

const Calibration& settingsGetCal() {
    if (!g_loaded) settingsLoad();
    return g_cal;
}

bool settingsSaveCal(const Calibration& cal) {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, /*readOnly=*/false)) {
        return false;
    }
    static const char* const z_keys[4] = {"cal_z0", "cal_z1", "cal_z2", "cal_z3"};
    static const char* const c_keys[4] = {"cal_c0", "cal_c1", "cal_c2", "cal_c3"};
    for (uint8_t i = 0; i < 4; i++) {
        prefs.putFloat(z_keys[i], cal.z[i]);
        prefs.putFloat(c_keys[i], cal.c[i]);
    }
    prefs.end();

    g_cal = cal;
    return true;
}

bool settingsHasStored() {
    if (!g_loaded) settingsLoad();
    return g_stored;
}

const SiloConfig& settingsGetSilo() {
    if (!g_loaded) settingsLoad();
    return g_silo;
}

bool settingsSaveSilo(const SiloConfig& silo) {
    if (!siloIsSane(silo)) {
        Serial.println("[settings] silo config refusee (hors bornes)");
        return false;
    }
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, false)) {
        Serial.println("[settings] NVS indisponible (silo)");
        return false;
    }
    // On n'écrit que ces clés, le reste de l'espace est intact.
    prefs.putFloat("silo_capacity", silo.capacity_kg);
    prefs.putFloat("silo_low", silo.low_kg);
    prefs.putFloat("silo_bag_kg", silo.bag_kg);
    prefs.end();
    g_silo = silo;
    Serial.printf("[settings] silo enregistre : capacite %.1f kg, seuil %.1f kg, sac %.1f kg\n",
                  g_silo.capacity_kg, g_silo.low_kg, g_silo.bag_kg);
    return true;
}

bool settingsSave(const Settings& s) {
    // Nettoie avant d'écrire : ce qui est stocké est utilisable tel quel.
    Settings clean = s;
    settingsNormalize(clean);

    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, /*readOnly=*/false)) {
        return false;
    }
    prefs.putString("wifi_ssid", clean.wifi_ssid);
    prefs.putString("wifi_pass", clean.wifi_pass);
    prefs.putString("mqtt_server", clean.mqtt_server);
    prefs.putString("mqtt_base", clean.mqtt_base);
    prefs.putString("mqtt_user", clean.mqtt_user);
    prefs.putString("mqtt_pass", clean.mqtt_pass);
    prefs.putUShort("mqtt_port", clean.mqtt_port);
    prefs.putUShort("aspiration_min", clean.aspiration_min);
    prefs.end();

    g_settings = clean;
    g_loaded = true;
    g_stored = true;
    return true;
}
