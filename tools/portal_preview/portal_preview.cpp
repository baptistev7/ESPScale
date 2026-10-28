// Banc PC du portail web : compile la VRAIE génération de page de
// src/webconfig.cpp (avec state.cpp et scheduler.cpp) contre des bouchons
// (stubs/), puis écrit le HTML de chaque onglet dans un fichier. Les captures
// sont faites ensuite par render.sh (Firefox headless).
#include "../../src/webconfig.cpp"
#include "../../src/state.cpp"
#include "../../src/scheduler.cpp"

SerialStub Serial;
EspStub ESP;
WiFiStub WiFi;

// --- Réglages et calibration d'exemple (aucune valeur réelle du site) -------
static Settings s_set;
static SiloConfig s_silo;
static Calibration s_cal;
const Settings& settingsGet() { return s_set; }
const SiloConfig& settingsGetSilo() { return s_silo; }
const Calibration& settingsGetCal() { return s_cal; }
bool settingsHasStored() { return true; }
bool settingsSave(const Settings&) { return true; }
bool settingsSaveSilo(const SiloConfig&) { return true; }
bool settingsSaveCal(const Calibration&) { return true; }
void settingsNormalize(Settings&) {}
void settingsLoad() {}
bool timeIsValid() { return true; }
void buttonEnable(bool) {}
uint8_t buttonTakeShorts() { return 0; }
bool buttonTakeLong() { return false; }

static void writePage(const char* path, const char* tab, const String& banner) {
    g_server.args.clear();
    g_server.args["t"] = tab;
    std::string html = pagePortal(settingsGet(), banner).v;
    // Calibration : les relevés arrivent en direct (/cal/readings), absents
    // hors carte. Pour la capture, on remplace le sondage par des valeurs
    // d'EXEMPLE (raw, kg) — documentées comme telles.
    const std::string poll = "poll(); setInterval(poll,2000);";
    const size_t at = html.find(poll);
    if (at != std::string::npos) {
        html.replace(at, poll.size(),
            "[[1199812,82.1],[1763201,80.7],[960321,83.9],[755312,80.7]]"
            ".forEach((v,i)=>{R[i][0]=v[0];R[i][1]=v[1];});render();");
    }
    FILE* f = fopen(path, "w");
    if (!f) { perror(path); exit(1); }
    fputs(html.c_str(), f);
    fclose(f);
    fprintf(stderr, "  %s\n", path);
}

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : ".";
    snprintf(s_set.wifi_ssid, sizeof(s_set.wifi_ssid), "MaBox-2G");
    snprintf(s_set.wifi_pass, sizeof(s_set.wifi_pass), "secret");
    snprintf(s_set.mqtt_server, sizeof(s_set.mqtt_server), "192.168.1.2");
    snprintf(s_set.mqtt_base, sizeof(s_set.mqtt_base), "scale");
    s_set.mqtt_port = 1883;
    s_set.aspiration_min = 18 * 60 + 30;
    s_silo = {670.0f, 134.0f, 15.0f};
    const float z[4] = {-498735.0f, 92822.0f, -818867.0f, -826467.0f};
    const float c[4] = {-20690.0f, -20690.0f, -21230.0f, -19600.0f};
    for (int i = 0; i < 4; i++) { s_cal.z[i] = z[i]; s_cal.c[i] = c[i]; }

    stateReset();
    g_state.has_last_measure = true;
    g_state.last_measure_kg = 327.0f;
    g_state.last_measure_at = time(nullptr) - (2 * 3600 + 34 * 60);
    g_state.has_last_weight = true;
    g_state.last_weight_kg = 327.0f;
    g_state.last_send_ok = true;
    g_state.disp_screen = DisplayScreen::MAIN;

    snprintf(g_ap_ssid, sizeof(g_ap_ssid), "Scale-A1B2");
    const struct { const char* ssid; int8_t rssi; bool open; uint8_t ch; } nets[] = {
        {"MaBox-2G", -48, false, 6}, {"Voisin_WiFi", -71, false, 11}, {"FreeWifi", -83, true, 1},
    };
    for (const auto& n : nets) {
        ScanEntry& e = g_scan[g_scan_count++];
        snprintf(e.ssid, sizeof(e.ssid), "%s", n.ssid);
        e.rssi = n.rssi; e.open = n.open; e.channel = n.ch;
    }

    char p[512];
    snprintf(p, sizeof(p), "%s/etat.html", out);     writePage(p, "etat", "");
    snprintf(p, sizeof(p), "%s/reglages.html", out); writePage(p, "reglages", "");
    snprintf(p, sizeof(p), "%s/cal.html", out);      writePage(p, "cal", "");
    return 0;
}
