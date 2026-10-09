// =============================================================================
// PORTAIL DE CONFIGURATION — coquille à onglets
// =============================================================================
// 4 onglets. Chaque onglet est UNE requête qui rend une page complète (~8 ko,
// ~200 ms sur un AP ouvert) : c'est le compromis qui évite d'embarquer 30 ko de
// HTML pour 4 écrans, tout en gardant une page lisible sans JavaScript — le
// choix « Choisir » d'un réseau et la confirmation de tare s'appuient dessus.
//
//   ÉTAT     ce que la carte sait maintenant (RAM RTC) + « rafraîchir l'écran »
//   RÉSEAU   scan des réseaux (RSSI / canal / sécurité) + SSID + mot de passe
//   MQTT·HA  broker + le TABLEAU DES TOPICS (mêmes chaînes que mqtt.cpp)
//   SILO     capacité utile, seuil « niveau bas », poids d'un sac
//
// Le point d'accès est protégé par un mot de passe tiré au hasard à chaque
// ouverture et affiché sur l'e-paper : seul quelqu'un devant la balance peut le
// rejoindre. Les mots de passe enregistrés (WiFi, MQTT) ne sont JAMAIS renvoyés
// dans la page : un champ laissé vide veut dire « inchangé ».
//
// La calibration des pieds garde sa page (/cal) : c'est une opération de banc,
// avec son protocole (repère avant/après), pas un formulaire de réglages.
//
// Volontairement absent (cf. docs/UI/WEBnn) : le miroir de l'écran e-paper, le
// « profil d'affichage », les seuils d'alarme côté HA, le CSV, le syslog. Ce
// portail configure et consulte ; au-delà du rafraîchissement d'écran, il
// n'exécute rien à distance.
// =============================================================================

#include "webconfig.hpp"

#include "settings.hpp"
#include "config.hpp"
#include "display.hpp"
#include "power.hpp"  // getBatteryPercentage / isCharging (en-tête de l'écran)
#include "mqtt.hpp"   // mqttTestConnection (bouton « tester » du portail)
#include "sensors.hpp" // lecture/calibration des pieds (page /cal, « tare silo vide »)
#include "scheduler.hpp" // schedulerSecondsUntilNextWake (onglet ÉTAT)
#include "state.hpp"  // g_state (onglet ÉTAT)
#include "time.hpp"   // timeIsValid / heure locale (onglet ÉTAT)
#include "button.hpp" // « ● COURT REDEMARRER » : sortie du portail

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <cstring>

// Durée d'inactivité du portail (partagée avec l'affichage « ARRÊT AUTO : N MIN »).
static const uint32_t kPortalTimeoutMs = PORTAL_TIMEOUT_MIN * 60UL * 1000UL;
static const uint32_t kStaTestTimeoutMs = 12000;              // test WiFi

static WebServer g_server(80);
static DNSServer g_dns;
static uint32_t  g_last_activity = 0;
static char      g_ap_ssid[24];
static char      g_ap_pass[9];   // 8 chiffres (minimum WPA2), tirés à chaque ouverture

// Réglages du silo SAISIS mais non enregistrés (test, enregistrement refusé) : la
// page réaffichée doit montrer ce que l'utilisateur a tapé, pas la NVS — sinon un
// « Enregistrer » suivant réécrirait les anciennes valeurs sans qu'il le voie.
static const SiloConfig* g_form_silo = nullptr;

// « Rafraîchir l'écran » remplace l'écran du portail (pied, mot de passe) par
// l'écran principal : on remet celui du portail après kEpdTestShowMs, sinon la
// dalle n'annonce plus comment fermer le portail. 0 = rien à restaurer.
static const uint32_t kEpdTestShowMs = 8000;
static uint32_t g_epd_test_at = 0;

// --- Réseaux détectés (scan mis en cache, rescannable à la demande) --------
// Le scan est fait UNE fois à l'entrée dans le portail (bloquant ~2-4 s) : la
// page peut ensuite être rechargée sans rescanner. « Rescanner » est proposé à
// l'utilisateur parce qu'un scan coupe le canal de l'AP le temps de l'opération.
static const uint8_t kMaxScan = 20;
struct ScanEntry {
    char    ssid[33];
    int8_t  rssi;
    bool    open;
    uint8_t channel;
};
static ScanEntry g_scan[kMaxScan];
static uint8_t   g_scan_count = 0;

static void scanWifiNetworks() {
    g_scan_count = 0;
    int16_t n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/false);
    if (n <= 0) {
        Serial.println("[portal] scan: aucun reseau");
        return;
    }
    for (int16_t i = 0; i < n && g_scan_count < kMaxScan; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0 || ssid.length() > 32) continue; // SSID masqué
        // Dédoublonne (un même SSID peut sortir sur plusieurs canaux/AP).
        bool dup = false;
        for (uint8_t k = 0; k < g_scan_count; k++) {
            if (strcmp(g_scan[k].ssid, ssid.c_str()) == 0) { dup = true; break; }
        }
        if (dup) continue;

        snprintf(g_scan[g_scan_count].ssid, sizeof(g_scan[g_scan_count].ssid),
                 "%s", ssid.c_str());
        g_scan[g_scan_count].rssi    = (int8_t)WiFi.RSSI(i);
        g_scan[g_scan_count].open    = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
        g_scan[g_scan_count].channel = (uint8_t)WiFi.channel(i);
        g_scan_count++;
    }
    WiFi.scanDelete();
    Serial.printf("[portal] scan: %u reseau(x)\n", g_scan_count);
}

// --- Utilitaires HTML ---------------------------------------------------------

static String htmlEscape(const char* s) {
    String out;
    for (const char* p = s; p && *p; ++p) {
        switch (*p) {
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '&': out += "&amp;"; break;
            case '"': out += "&quot;"; break;
            // Les attributs `value='...'` sont délimités par une apostrophe :
            // sans cet échappement, un mot de passe contenant ' casserait
            // l'attribut (et le formulaire).
            case '\'': out += "&#39;"; break;
            default:  out += *p;
        }
    }
    return out;
}

// « 1110 » -> « 18h30 ». Les deux créneaux sont rendus par RÔLE (« avant » /
// « après ») et non triés : près de minuit, une aspiration à 00h15 donne « avant
// 23h45 » et « après 00h45 », où l'ordre des nombres est l'inverse de l'ordre
// chronologique.
static String hhmm(uint16_t minutes) {
    char b[12];
    snprintf(b, sizeof(b), "%02uh%02u", minutes / 60, minutes % 60);
    return String(b);
}

// « 1110 » -> « 18:30 », le format EXIGE par <input type=time> (« 18h30 » y est
// rejeté et le champ s'affiche vide). hhmm() ci-dessus reste le format lisible.
static String hhmmColon(uint16_t minutes) {
    char b[12];
    snprintf(b, sizeof(b), "%02u:%02u", minutes / 60, minutes % 60);
    return String(b);
}

// Un horaire « HH:MM » (ce que renvoie <input type=time>) -> minutes depuis
// minuit. Renvoie false si le format est illisible ou hors bornes : mieux vaut
// refuser la saisie que d'enregistrer une aspiration à « 25h ».
static bool parseHhmm(const String& v, uint16_t& out) {
    const int colon = v.indexOf(':');
    if (colon <= 0) return false;
    const long h = v.substring(0, colon).toInt();
    const long m = v.substring(colon + 1).toInt();   // ignore un éventuel ":00"
    if (h < 0 || h > 23 || m < 0 || m > 59) return false;
    out = (uint16_t)(h * 60 + m);
    return true;
}

// Une ligne « libellé / valeur » de la table de l'onglet ÉTAT.
static String kv(const char* k, const String& v) {
    return String(F("<tr><th>")) + k + String(F("</th><td>")) + v + F("</td></tr>");
}

// Le bandeau de résultat (confirmation d'enregistrement, erreur de saisie).
static String bannerFromQuery() {
    const String ok = g_server.arg("ok");
    if (!ok.length()) return "";
    String out;
    if (ok == "save") {
        out = F("<div class=ok><b>Réglages enregistrés.</b> La carte redémarre "
                "(nouveau réseau / broker).</div>");
    } else if (ok == "epd") {
        out = F("<div class=ok><b>Écran rafraîchi.</b> L'image affichée est "
                "celle du dernier poids connu ; l'écran du portail revient "
                "après quelques secondes.</div>");
    } else if (ok == "scan") {
        out = F("<div class=ok>Réseaux rescannés.</div>");
    } else if (ok == "ko") {
        out = String(F("<div class=ko><b>Refusé.</b> ")) + htmlEscape(g_server.arg("why").c_str()) +
              F("</div>");
    }
    return out;
}

// --- Onglets -----------------------------------------------------------------

// ÉTAT : ce que la carte sait maintenant. Tout est lu de la RAM RTC et des
// capteurs — aucune valeur n'est inventée, et un champ absent affiche « -- ».
static String tabEtat(const Settings& s) {
    const SiloConfig& silo = settingsGetSilo();
    String h;
    h.reserve(1800);

    h += F("<div class=card><h2>Mesure</h2><table>");
    if (g_state.has_last_measure) {
        h += kv("Poids", String(g_state.last_measure_kg, 1) + " kg");
        const float cap = (silo.capacity_kg > 0.0f) ? silo.capacity_kg : TANK_FULL_KG;
        int pct = (int)((g_state.last_measure_kg / cap) * 100.0f);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        h += kv("Niveau", String(pct) + " %");
        h += kv("Seuil « niveau bas »", String(silo.low_kg, 1) + " kg");
        h += kv("Poids d'un sac", String(silo.bag_kg, 1) + " kg (repère)");
        // La date n'est affichée QUE si l'horloge est fiable : sans NTP, le
        // RTC repart de l'époque et l'on montrerait un « 01/01 00:12 » et une
        // âge absurde. Mieux vaut « horloge non synchronisée » que du faux.
        if (g_state.last_measure_at > 0 && timeIsValid()) {
            struct tm t;
            localtime_r(&g_state.last_measure_at, &t);
            char when[24];
            strftime(when, sizeof(when), "%d/%m %H:%M", &t);
            const long age = (long)((time(nullptr) - g_state.last_measure_at) / 60);
            char agetxt[24];
            if (age < 1)        snprintf(agetxt, sizeof(agetxt), "a l'instant");
            else if (age < 60)  snprintf(agetxt, sizeof(agetxt), "%ld min", age);
            else if (age < 2880) snprintf(agetxt, sizeof(agetxt), "%ld h %02ld", age / 60, age % 60);
            else                snprintf(agetxt, sizeof(agetxt), "%ld j", age / 1440);
            h += kv("Mesurée le", String(when) + " (il y a " + agetxt + ")");
        } else {
            h += kv("Mesurée le", "horloge non synchronisée (date inconnue)");
        }
    } else {
        h += kv("Poids", "--");
        h += kv("Niveau", "--");
        h += kv("Seuil « niveau bas »", String(silo.low_kg, 1) + " kg");
        h += kv("Poids d'un sac", String(silo.bag_kg, 1) + " kg (repère)");
        h += kv("Mesure", "jamais mesurée (capteurs non disponibles)");
    }
    h += F("</table></div>");

    h += F("<div class=card><h2>Alimentation &amp; publication</h2><table>");
    h += kv("Batterie", String(getBatteryPercentage()) + " %  (" +
                      String(readBatteryVoltage(), 2) + " V)");
    h += kv("Charge USB", isCharging() ? "oui" : "non");
    if (!g_state.has_last_weight) {
        h += kv("Dernier envoi MQTT", "rien envoyé (1re mesure pas faite)");
    } else {
        h += kv("Poids publié", String(g_state.last_weight_kg, 1) + " kg");
        h += kv("Dernier envoi", g_state.last_send_ok ? "OK" : "ÉCHEC (sera rejoué)");
        h += kv("Cycles sans envoi", String(g_state.skip_count) + " (cliquet anti-dérive)");
    }
    h += F("</table></div>");

    h += F("<div class=card><h2>Système</h2><table>");
    h += kv("Firmware", String(FW_VERSION));
    h += kv("Mode", stateModeName(stateGetMode()));
    h += kv("Aspiration", hhmm(s.aspiration_min));
    h += kv("Mesures à", hhmm(schedulerWakeSlotFrom(s.aspiration_min, 0)) + " et " +
                         hhmm(schedulerWakeSlotFrom(s.aspiration_min, 1)) + " (±30 min)");
    h += kv("Prochain réveil", String(schedulerSecondsUntilNextWake() / 60) + " min");
    h += kv("Écran affiché", g_state.disp_screen == DisplayScreen::UNKNOWN
                  ? "inconnu (redémarrage)" : "connu (partial possible)");
    char heap[24];
    snprintf(heap, sizeof(heap), "%u ko", (unsigned)(ESP.getFreeHeap() / 1024));
    h += kv("RAM libre", heap);
    h += F("</table></div>");

    h += F("<div class=card><h2>Écran e-paper</h2><div class=sub>Le cycle de "
           "mesure est automatique (");
    h += hhmm(schedulerWakeSlotFrom(s.aspiration_min, 0)) + F(" et ") +
         hhmm(schedulerWakeSlotFrom(s.aspiration_min, 1));
    h += F("). Ce bouton ne refait PAS de mesure : il redessine l'écran avec le "
           "dernier poids connu, pour vérifier l'affichage sans attendre. "
           "L'écran du portail revient après quelques secondes.</div>"
           "<form method=post action=/epd>"
           "<button type=submit>Rafraîchir l'écran</button></form></div>");

    return h;
}

// RÉSEAU : le scan en TABLEAU (choisin en un clic), puis le formulaire.
// Le champ SSID reste libre : un réseau masqué ne peut pas être détecté.
// RÉGLAGES : WiFi + MQTT + silo dans UN SEUL formulaire, donc UN SEUL
// enregistrement et UN SEUL redémarrage. Séparer ces trois pages obligeait à
// redémarrer trois fois, et le redémarrage est le seul chemin de sortie du
// portail : c'est le geste le plus cher de l'interface.
//
// Un <form> dans un <form> est du HTML invalide : le bouton « Enregistrer »,
// placé hors du formulaire, s'y rattache par `form=cfg`.
static String tabReglages(const Settings& s, const SiloConfig& silo) {
    const bool auth = (s.mqtt_user[0] != '\0');
    String h;
    h.reserve(5200);

    h += F("<form id=cfg method=post action=\"/save?t=reglages\">");

    // --- WiFi ---------------------------------------------------------------
    h += F("<div class=card><h2>1. Réseau WiFi</h2>");
    h += F("<div class=sub>Réseaux détectés ");
    h += String(g_scan_count);
    // Un rescan est une LECTURE : c'est un lien, surtout pas un <form> — un
    // formulaire imbriqué dans le formulaire principal ferait fermer celui-ci par
    // le parseur HTML (les champs suivants sortiraient du form, et le bouton
    // « Enregistrer » n'enverrait plus que le SSID).
    h += F(" <a class=mini href=\"/scan?t=reglages\">Rescanner</a></div>");
    if (g_scan_count == 0) {
        h += F("<div class=warn>Aucun réseau détecté. Si le SSID est masqué, "
               "saisissez-le à la main — et vérifiez que la box émet bien en "
               "<b>2,4 GHz</b> (le T5 ne voit pas le 5 GHz).</div>");
    } else {
        h += F("<table class=grid><tr><th>Réseau</th><th>Signal</th><th>Canal</th>"
               "<th>Sécurité</th><th></th></tr>");
        for (uint8_t i = 0; i < g_scan_count; i++) {
            // Le SSID va dans un ATTRIBUT (data-ssid), jamais comme littéral
            // JavaScript : « pick(Mon Reseau) » est une erreur de syntaxe, et
            // « pick(2G_Wifi) » aussi (identifiant invalide). En attribut, un
            // espace ou un guillemet n'a rien de spécial, et le guillemet double
            // reste échappé par htmlEscape().
            h += "<tr class=row data-ssid='" + htmlEscape(g_scan[i].ssid) +
                 "' onclick=\"pick(this)\">"
                 "<td><b>" + htmlEscape(g_scan[i].ssid) + "</b></td><td>" +
                 String((int)g_scan[i].rssi) + " dBm</td><td>Ch " +
                 String(g_scan[i].channel) + "</td><td>" +
                 (g_scan[i].open ? "ouvert" : "WPA2+") + "</td>" +
                 "<td><button type=button class=mini>Choisir</button></td></tr>";
        }
        h += F("</table>");
    }
    h += F("<label>SSID</label><input id=ssid name=wifi_ssid value='");
    h += htmlEscape(s.wifi_ssid) + F("'>");
    // Le mot de passe enregistré n'est jamais renvoyé : champ vide = inchangé.
    h += F("<label>Mot de passe</label><input name=wifi_pass type=password "
           "autocomplete=new-password placeholder='");
    h += s.wifi_pass[0] ? F("(inchangé si vide)") : F("");
    h += F("'>");
    h += F("</div>");

    // --- MQTT ---------------------------------------------------------------
    h += F("<div class=card><h2>2. Broker MQTT &middot; Home Assistant</h2>");
    h += "<label>Serveur (hôte ou IP)</label><input name=mqtt_server value='" +
         htmlEscape(s.mqtt_server) + "'>";
    h += "<label>Port</label><input name=mqtt_port type=number min=1 max=65535 value='" +
         String(s.mqtt_port) + "'>";
    h += "<label>Base des topics</label><input name=mqtt_base value='" +
         htmlEscape(s.mqtt_base) + "'>";
    // Champ caché : distingue « case MQTT décochée » de « formulaire qui ne
    // parle pas d'authentification » (une case non cochée n'est pas envoyée).
    h += F("<input type=hidden name=mqtt_form value=1>");
    h += F("<label class=chk><input type=checkbox name=mqtt_auth "
           "id=mqtt_auth onchange=authToggle()");
    if (auth) h += F(" checked");
    h += F(">Authentification</label><div id=authfields>");
    h += "<label>Utilisateur</label><input name=mqtt_user value='" +
         htmlEscape(s.mqtt_user) + "'>";
    h += F("<label>Mot de passe</label><input name=mqtt_pass type=password "
           "autocomplete=new-password placeholder='");
    h += s.mqtt_pass[0] ? F("(inchangé si vide)") : F("");
    h += F("'>");
    h += F("<div class=sub>Utilisateur vide = broker sans authentification.</div></div>");
    // Le test ne concerne QUE le réseau et le broker : il est donc posé dans
    // cette carte, juste sous les champs qu'il vérifie, et il n'écrit rien.
    // Attributs entre guillemets doubles : le texte contient des apostrophes.
    h += F("<button class='wide sec' type=submit formnovalidate "
           "formaction=\"/test?t=reglages\" "
           "data-mt=\"Tester le WiFi et le broker ?\" "
           "data-mm=\"Connexion au WiFi puis au broker MQTT avec les valeurs saisies "
           "(~30 s). Rien n'est enregistré : ni le réseau, ni le broker, ni le "
           "silo.\">Tester WiFi + MQTT (sans enregistrer)</button>"
           "<div class=sub>Vérifie seulement la connexion. Rien n'est enregistré.</div>");
    h += F("</div>");

    // --- Silo ---------------------------------------------------------------
    h += F("<div class=card><h2>3. Le silo</h2>");
    h += "<label>Capacité utile (kg de granulés)</label>"
         "<input name=capacity type=number step=1 min=10 max=5000 value='" +
         String(silo.capacity_kg, 1) + "'>";
    h += "<label>Seuil « niveau bas » (kg)</label>"
         "<input name=low type=number step=1 min=0 max=5000 value='" +
         String(silo.low_kg, 1) + "'>";
    h += "<label>Poids d'un sac (kg)</label>"
         "<input name=bag type=number step=0.1 min=1 max=50 value='" +
         String(silo.bag_kg, 1) + "'>";
    h += F("<div class=sub>Le zéro de la balance se règle dans l'onglet "
           "CALIBRATION (tare des 4 pieds).<br>"
           "Le poids d'un sac n'est qu'un <b>repère</b> affiché pendant le "
           "remplissage (« 12 sacs × 15 kg »), pour que vous puissiez comparer "
           "au poids versé. Le prix, lui, se saisit à l'écran et ne vient pas "
           "de là.</div>");
    h += F("</div>");   // fin card silo

    // --- Horaire ------------------------------------------------------------
    h += F("<div class=card><h2>4. Aspiration quotidienne</h2>"
           "<label>Heure de l'aspiration</label>"
           "<input name=aspiration type=time step=60 value='");
    h += hhmmColon(s.aspiration_min) + F("'>");
    h += F("<div class=sub>La balance ne mesure pas à cet instant : elle se "
           "réveille <b>30 min avant</b> et <b>30 min après</b>, pour avoir deux "
           "bornes de consommation propres (la journée d'un côté, la conso de la "
           "nuit de l'autre). Avec 18h30, elle mesure à 18h00 et 19h00. "
           "Déplacez cet horaire si l'aspiration est programmable ou tardive.</div>");
    h += F("<table class=derived><tr><th>Mesures à</th><td>");
    h += hhmm(schedulerWakeSlotFrom(s.aspiration_min, 0)) + F(" &nbsp;/&nbsp; ") +
         hhmm(schedulerWakeSlotFrom(s.aspiration_min, 1));
    h += F("</td></tr></table></div>");

    h += F("</form>");  // fin formulaire unique

    // --- Un seul enregistrement, un seul redémarrage ------------------------
    h += F("<div class=card><h2>Enregistrer</h2>"
           "<div class=sub>Les quatre sections sont enregistrées <b>ensemble</b>. "
           "La connexion WiFi est testée avant écriture : si elle échoue, "
           "<b>rien</b> n'est enregistré (silo compris). Sinon la carte redémarre "
           "et reprend ses mesures.</div>"
           "<button class=wide type=submit form=cfg "
           "data-mt=\"Enregistrer et redémarrer ?\" "
           "data-mm=\"La balance va se reconnecter au réseau, puis redémarrer. "
           "Ses mesures reprennent dans une seconde.\">Enregistrer et redémarrer"
           "</button></div>");

    return h;
}

// Onglet CALIBRATION : la calibration est rendue DANS la coquille, plus de
// page autonome (donc plus de <head>/<style>/lien de retour dupliques).
// Son <script> reste ici : il n'est evalue qu'au rechargement de l'onglet.
static String tabCal(const String& banner) {
    (void)banner;
    const Calibration& cal = settingsGetCal();

    String html;
    html.reserve(5200);
    html += F("<div class=card><h2>Calibration des pieds</h2>"
              "<div class=sub><b>Silo en place.</b> On tare les <b>4 pieds d'un "
              "coup</b> (rien de pos&eacute; en plus), puis on pose le poids de test "
              "successivement sur chaque pied en mesurant la <b>diff&eacute;rence</b> "
              "avant/apr&egrave;s.</div></div>");

    html += F("<div class=card><div class=warn>&#9888;&#65039; <b>Modifier Z d&eacute;cale "
              "le poids publi&eacute;.</b> Le cliquet ne publiant que des baisses, une "
              "baisse de tare serait compt&eacute;e comme de la <b>consommation</b> par "
              "Home Assistant. C'est une op&eacute;ration de <b>mise en service</b> "
              "(ou &agrave; faire en acceptant le d&eacute;calage).</div></div>");

    // --- Relevés live : avant → maintenant pour raw ET kg, plus le delta ---
    // Les deux grandeurs montrent la valeur FIGÉE au repère puis la valeur
    // courante : on vérifie visuellement l'avant/après sans se fier au seul Δ.
    html += F("<div class=card><h2>Mesures <span id=cnt class='mut sm'></span></h2>"
              "<table class=grid><tr><th>Pied</th><th>raw</th><th>&Delta; raw</th>"
              "<th>kg</th></tr>");
    for (uint8_t i = 0; i < 4; i++) {
        html += "<tr id=tr" + String(i) + "><td><b>P" + String(i + 1) +
                "</b></td><td class=cmp id=xr" + String(i) + ">-</td>"
                "<td class=cmp id=xd" + String(i) + ">-</td>"
                "<td class=cmp id=xk" + String(i) + ">-</td></tr>";
    }
    html += F("<tr class=tot><td>TOTAL</td><td class=cmp id=xtr>-</td>"
              "<td class=cmp id=xtd>-</td><td class=cmp id=xtk>-</td></tr></table>"
              "<div class='sub' style='margin-top:8px'>En gris : valeur fig&eacute;e au "
              "rep&egrave;re. En gras : valeur actuelle.</div>"
              "<div id=mkstat class='sub' style='margin-top:6px'>Rep&egrave;re : "
              "<b>non pris</b> &mdash; clique sur &laquo; Rep&egrave;re &raquo; avant "
              "de poser le poids.</div>"
              "<div class=two>"
              "<button type=button class=sec onclick='mark()'>&#9201; Rep&egrave;re "
              "(avant)</button>"
              "<button type=button class=sec onclick='tareAll()'>&#8776; Tare des "
              "4 pieds</button>"
              "</div>"
              "<div class=two><button type=button class=sec onclick='clearMark()'>"
              "Effacer le rep&egrave;re</button>"
              "<button type=button class=sec onclick='poll()'>&#8635; Rafra&icirc;chir"
              "</button></div>"
              "<div class='sub' style='margin-top:10px'>"
              "<b>1.</b> Tare des 4 pieds (silo en place, rien d'ajout&eacute;)."
              "<br><b>2.</b> Rep&egrave;re (avant), puis pose le poids sur UN pied : "
              "la ligne du pied concern&eacute; s'allume et le &Delta; des autres "
              "montre l'influence." "</div></div>");

    // --- Poids de test ---
    // Le champ demande le poids REELLEMENT pose (pas une valeur imposee) : c'est
    // lui qui sert au calcul de C. Defaut = le poids de reference dispo (1,96 kg),
    // avec la recommandation ~10 kg (l'erreur sur C diminue quand le poids monte).
    html += F("<div class=card><h2>Poids de test</h2>"
              "<label>Poids pos&eacute; sur le pied (kg)</label>"
              "<input id=known type=number step=0.01 min=0.01 value=1.96>"
              "<div class='sub' style='margin-top:6px'>"
              "Indique le poids que tu poses sur le pied : il sert au calcul de C."
              "<br><b>Recommand&eacute; : ~10 kg</b> (ex. un bidon de 10 L d'eau) "
              "&mdash; l'erreur sur C diminue quand le poids augmente. "
              "1,96 kg suffit pour v&eacute;rifier qu'un pied r&eacute;pond."
              "</div></div>");

    // --- Facteurs ---
    html += F("<div class=card><h2>Facteurs &middot; Z offset / C &eacute;chelle</h2>"
              "<form method=post action=/cal/save>"
              "<table class=grid><tr><th>Pied</th><th>Z</th><th>C</th><th></th></tr>");
    for (uint8_t i = 0; i < 4; i++) {
        html += "<tr><td><b>P" + String(i + 1) + "</b></td>"
                "<td><input name=z" + String(i) + " id=z" + String(i) +
                " value='" + String(cal.z[i], 1) + "'></td>"
                "<td><input name=c" + String(i) + " id=c" + String(i) +
                " value='" + String(cal.c[i], 2) + "'></td>"
                "<td><button type=button class=mini onclick='calcC(" + String(i) +
                ")'>&Delta; &rarr; C</button></td></tr>";
    }
    html += F("</table><button class=wide type=submit>Enregistrer la "
              "calibration</button></form></div>");

    // --- JS : sondage, repère avant/après (raw ET kg), tare 4 pieds, calcul de C ---
    html += F("<script>\n"
              "const R=[[NaN,NaN],[NaN,NaN],[NaN,NaN],[NaN,NaN]];/* raw,kg courant */\n"
              "const B=[NaN,NaN,NaN,NaN];/* repère raw, FIGÉ */\n"
              "const K=[NaN,NaN,NaN,NaN];/* repère kg,  FIGÉ */\n"
              "let mkAt=null, mkSumR=NaN, mkSumK=NaN;/* heure + totaux au repère */\n"
              // Cellule « avant → maintenant » : repère en gris, valeur courante
              // en gras. Sans repère, seule la valeur courante est montrée.
              "function cellCmp(a,b,dec){\n"
              " if(!isFinite(b))return '<span class=mut>-</span>';\n"
              " if(!isFinite(a))return '<b>'+b.toFixed(dec)+'</b>';\n"
              " return '<span class=mut>'+a.toFixed(dec)+'</span>'+"
              "'<span class=ar> &rarr; </span><b>'+b.toFixed(dec)+'</b>';\n"
              "}\n"
              // render() ne fait QUE l'affichage, à partir de R/B/K : poll() lit
              // puis render(), mark() fige puis render().
              "function render(){\n"
              " let ok=0,srR=0,skR=0,sd=0,nb=0;\n"
              " for(let i=0;i<4;i++){\n"
              "  const raw=R[i][0], kg=R[i][1];\n"
              "  document.getElementById('xr'+i).innerHTML=cellCmp(B[i],raw,0);\n"
              "  document.getElementById('xk'+i).innerHTML=cellCmp(K[i],kg,2);\n"
              "  let d='-';\n"
              "  if(isFinite(raw)&&isFinite(B[i])){d=(raw-B[i]).toFixed(0);sd+=raw-B[i];nb++;}\n"
              "  document.getElementById('xd'+i).textContent=d;\n"
              "  if(isFinite(raw)){ok++;srR+=raw;}\n"
              "  if(isFinite(kg))skR+=kg;\n"
              " }\n"
              " document.getElementById('cnt').textContent='('+ok+'/4)';\n"
              " document.getElementById('xtr').innerHTML=cellCmp(mkSumR,srR,0);\n"
              " document.getElementById('xtk').innerHTML=cellCmp(mkSumK,skR,2);\n"
              " document.getElementById('xtd').textContent=nb?sd.toFixed(0):'-';\n"
              // Met en évidence le pied dont le raw a le PLUS bougé depuis le repère.
              " let best=-1,bd=0;\n"
              " for(let i=0;i<4;i++){if(!isFinite(B[i])||!isFinite(R[i][0]))continue;\n"
              "  const a=Math.abs(R[i][0]-B[i]); if(a>bd){bd=a;best=i;}}\n"
              " for(let i=0;i<4;i++)"
              "document.getElementById('tr'+i).className=(i===best)?'on':'';\n"
              " const st=document.getElementById('mkstat');\n"
              " st.innerHTML=mkAt?('Rep&egrave;re pris &agrave; <b>'+mkAt+'</b> "
              "&mdash; colonne de gauche = valeur fig&eacute;e.'):"
              "('Rep&egrave;re : <b>non pris</b> &mdash; clique sur &laquo; Rep&egrave;re "
              "&raquo; avant de poser le poids.');\n"
              "}\n"
              "async function poll(){try{\n"
              " const t=await (await fetch('/cal/readings')).text();\n"
              " const L=t.trim().split('\\n');\n"
              " for(let i=0;i<4;i++){\n"
              "  const p=(L[i]||'- -').trim().split(/\\s+/);\n"
              "  R[i][0]=parseFloat(p[0]); R[i][1]=parseFloat(p[1]);\n"
              " }\n"
              " render();\n"
              " }catch(e){}}\n"
              // mark() prend une mesure FRAICHE puis fige raw ET kg : sinon,
              // cliqué avant le premier relevé, il figeait NaN pour toujours.
              "async function mark(){\n"
              " await poll();\n"
              " let any=false, sr=0, sk=0;\n"
              " for(let i=0;i<4;i++){\n"
              "  B[i]=R[i][0]; K[i]=R[i][1];\n"
              "  if(isFinite(B[i]))any=true;\n"
              "  if(isFinite(R[i][0]))sr+=R[i][0];\n"
              "  if(isFinite(R[i][1]))sk+=R[i][1];\n"
              " }\n"
              " if(!any){say('Aucun pied disponible','Le repere est impossible : les 4 pieds doivent repondre.');return;}\n"
              " mkSumR=sr; mkSumK=sk;\n"
              " const d=new Date();\n"
              " mkAt=String(d.getHours()).padStart(2,'0')+':'+"
              "String(d.getMinutes()).padStart(2,'0')+':'+"
              "String(d.getSeconds()).padStart(2,'0');\n"
              " render();\n"
              "}\n"
              "function clearMark(){for(let i=0;i<4;i++){B[i]=NaN;K[i]=NaN;}"
              "mkAt=null;mkSumR=NaN;mkSumK=NaN;render();}\n"
              "function tareAll(){for(let i=0;i<4;i++){if(isFinite(R[i][0]))"
              "document.getElementById('z'+i).value=R[i][0].toFixed(1);}}\n"
              "function calcC(i){\n"
              " const w=parseFloat(document.getElementById('known').value);\n"
              " if(!isFinite(w)||w<=0){say('Poids de test invalide','Indique le poids reellement pose, en kg (> 0).');return;}\n"
              " if(!isFinite(R[i][0])){say('Pied P'+(i+1)+' indisponible','Ce pied ne repond pas : repose le, ou ne calcule pas C dessus.');return;}\n"
              " let d;\n"
              " if(isFinite(B[i]))d=R[i][0]-B[i];\n"
              " else{const z=parseFloat(document.getElementById('z'+i).value);d=R[i][0]-z;}\n"
              " if(!isFinite(d)||Math.abs(d)<100){"
              "say('Delta trop faible','Clique sur Repere puis pose le poids sur CE pied : le delta est trop petit pour calculer C.');return;}\n"
              " document.getElementById('c'+i).value=(d/w).toFixed(2);\n"
              "}\n"
              "poll(); setInterval(poll,2000);\n"
              "</script>");

    return html;
}

// --- Page ---------------------------------------------------------------------
// Un onglet par requête : `?t=etat|reglages|cal`. Tout est rendu d'un coup.
static String pagePortal(const Settings& s, const String& banner) {
    String active = g_server.arg("t");
    if (!active.length()) active = "etat";
    const uint8_t idx = (active == "reglages") ? 1 : (active == "cal") ? 2 : 0;

    String html;
    html.reserve(10000);
    html += F("<!DOCTYPE html><html lang=fr><head><meta charset=utf-8>"
              "<meta name=viewport content='width=device-width,initial-scale=1'>"
              "<title>Balance Silo &middot; portail</title><style>"
              ":root{--bg:#f3f5f8;--fg:#16202b;--mut:#6b7684;--line:#e3e7ec;"
              "--acc:#0b6bcb;--ok:#177245;--warn:#8a5300}"
              "*{box-sizing:border-box}"
              "body{font-family:ui-monospace,'JetBrains Mono',Menlo,monospace;"
              "margin:0;padding:0 0 28px;background:var(--bg);color:var(--fg);"
              "font-size:14px;-webkit-text-size-adjust:100%}"
              ".wrap{max-width:720px;margin:0 auto;padding:0 14px}"
              "header{background:#fff;border-bottom:2px solid var(--fg);"
              "padding:12px 14px;margin-bottom:12px}"
              "h1{font-size:.98rem;margin:0;letter-spacing:.04em}"
              "h2{font-size:.76rem;margin:0 0 10px;color:var(--mut);"
              "text-transform:uppercase;letter-spacing:.08em}"
              ".sub{color:var(--mut);font-size:.8rem;line-height:1.45}"
              "nav{display:flex;flex-wrap:wrap;gap:0;margin:0 -14px 14px;"
              "background:var(--fg)}"
              "nav a{flex:1 1 33%;padding:11px 6px;text-align:center;color:#fff;"
              "text-decoration:none;font-size:.74rem;letter-spacing:.08em}"
              "nav a.on{background:var(--acc)}"
              ".card{background:#fff;border:1px solid var(--line);padding:14px;"
              "margin-bottom:12px}"
              "table{width:100%;border-collapse:collapse}"
              "th{color:var(--mut);font-size:.66rem;text-transform:uppercase;"
              "letter-spacing:.05em;text-align:left;padding:0 6px 6px 0;"
              "font-weight:600;width:44%}"
              "td{padding:6px 0;border-top:1px solid var(--line);"
              "font-variant-numeric:tabular-nums}"
              // Les tableaux en colonnes (scan, calibration) n'ont pas de libellé
              // à gauche : la largeur de 44 % de `th` y écrasait les valeurs.
              "table.grid th{width:auto;padding-right:6px}"
              "table.grid td{padding-right:4px}"
              "td input{padding:6px 7px;font-size:.82rem;min-width:0}"
              "table.derived{margin:12px 0 2px}"
              "table.derived th{width:auto;padding-right:8px}"
              "code{background:#f0f3f6;padding:1px 4px;border-radius:3px}"
              ".mut{color:var(--mut)}.sm{font-size:.72rem}"
              ".ar{color:var(--mut);font-size:.7rem}"
              "label{display:block;font-size:.76rem;color:var(--mut);margin:12px 0 5px}"
              "label.chk{display:flex;align-items:center;gap:8px;margin:16px 0 5px}"
              "label.chk input{width:auto;margin:0}"
              "input{padding:9px;font-size:.9rem;border:1px solid var(--line);"
              "width:100%;font-variant-numeric:tabular-nums;background:#fcfdfe}"
              "button{border:0;background:var(--acc);color:#fff;padding:11px 14px;"
              "font-size:.86rem;cursor:pointer;font-weight:600;margin-top:10px}"
              "button.sec{background:#e9edf2;color:var(--fg)}"
              "button.mini{padding:5px 9px;font-size:.72rem;margin:0;white-space:nowrap;"
              "background:#e9edf2;color:var(--fg);font-weight:500}"
              "button.wide{display:block;width:100%;margin-top:10px}"
              ".two{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:10px}"
              ".two button{margin-top:0}"
              "a.mini{display:inline-block;padding:5px 9px;font-size:.72rem;"
              "background:#e9edf2;color:var(--fg);font-weight:500;text-decoration:none}"
              "tr.row{cursor:pointer}"
              "tr.on td{background:#eff6ff}"
              "tr.tot td{border-top:2px solid var(--fg);font-weight:700}"
              "td.cmp{font-size:.75rem;white-space:nowrap}"
              ".ok{background:#eaf7ef;border-left:4px solid var(--ok);padding:10px 12px;"
              "margin-bottom:10px;font-size:.84rem}"
              ".ko{background:#fdecec;border-left:4px solid #c62828;padding:10px 12px;"
              "margin-bottom:10px;font-size:.84rem}"
              ".warn{background:#fff7e6;border-left:4px solid var(--warn);"
              "padding:10px 12px;font-size:.8rem;line-height:1.45}"
              "ul{padding-left:18px;margin:6px 0}"
              "a{color:var(--acc)}"
              // --- Popup de confirmation -----------------------------------
              ".mask{position:fixed;inset:0;background:rgba(16,22,30,.55);"
              "display:flex;align-items:center;justify-content:center;padding:18px;"
              "z-index:9}"
              ".mask[hidden]{display:none}"
              ".modal{background:#fff;border:2px solid var(--fg);padding:16px;"
              "max-width:420px;width:100%}"
              ".modal h2{margin-bottom:8px;color:var(--fg);font-size:.82rem}"
              ".mbtns{display:flex;gap:8px;margin-top:16px}"
              ".mbtns button{flex:1;margin-top:0}"
              "</style></head><body><div class=wrap>");

    // En-tête : ce qu'on est en train de configurer.
    html += F("<header><h1>BALANCE SILO</h1><div class=sub>Portail local de "
              "configuration &middot; ");
    html += String("AP ") + htmlEscape(g_ap_ssid);
    html += F("</div></header>");

    static const char* const kTabs[3] = {"etat", "reglages", "cal"};
    static const char* const kLabels[3] = {"ÉTAT", "RÉGLAGES", "CALIBRATION"};
    html += F("<nav>");
    for (uint8_t i = 0; i < 3; i++) {
        const char* on = (i == idx) ? " class=on" : "";
        html += "<a href='/?t=" + String(kTabs[i]) + "'" + on + ">" + kLabels[i] + "</a>";
    }
    html += F("</nav>");

    html += banner;

    switch (idx) {
        case 1:  html += tabReglages(s, g_form_silo ? *g_form_silo : settingsGetSilo()); break;
        case 2:  html += tabCal(banner);                  break;
        default: html += tabEtat(s);                      break;
    }

    html += F("<div class=sub>Arrêt automatique après ");
    html += String(PORTAL_TIMEOUT_MIN);
    html += F(" min d'inactivité (redémarrage).</div>");

    // --- Popup : une seule instance, réutilisée par toutes les confirmations --
    // Remplace les confirm() natifs, qui affichent le nom du domaine du navigateur
    // (« example.org dit… ») et ne suivent pas le style de la page.
    html += F("<div class=mask id=mask hidden><div class=modal>"
              "<h2 id=mtitle></h2><div class=sub id=mbody></div>"
              "<div class=mbtns><button type=button class=sec id=mno>Annuler</button>"
              "<button type=button id=mok>Confirmer</button></div></div></div>");

    html += F("<script>"
              // pick() : le SSID est lu dans l'attribut data-ssid, jamais passé
              // comme littéral JS (un espace casserait la syntaxe).
              "function pick(row){var f=document.getElementById('ssid');"
              "f.value=row.dataset.ssid;f.focus();}"
              "function authToggle(){document.getElementById('authfields')"
              ".style.display=document.getElementById('mqtt_auth').checked"
              "? 'block':'none';}authToggle();"
              // openModal(title, body, onOk) : masqué par défaut, l'attribut
              // [hidden] suffit (le .mask[hidden]{display:none} le neutralise).
              "var mask=document.getElementById('mask'),mt=document.getElementById('mtitle'),"
              "mb=document.getElementById('mbody'),cb=null;"
              "function openModal(t,b,f){mt.textContent=t;mb.textContent=b;cb=f;"
              "mask.hidden=false;document.getElementById('mok').focus();}"
              "function closeModal(){mask.hidden=true;cb=null;}"
              "document.getElementById('mno').onclick=closeModal;"
              "document.getElementById('mok').onclick=function(){var f=cb;"
              "closeModal();if(f)f();};"
              "mask.onclick=function(e){if(e.target===mask)closeModal();};"
              // Un bouton qui porte data-mt demande confirmation avant d'envoyer.
              // bypass évite la boucle : le re-envoi ne doit plus repasser ici.
              "var bypass=false;"
              "document.addEventListener('submit',function(e){"
              " var b=e.submitter;"
              " if(bypass||!b||!b.dataset.mt)return;"
              " e.preventDefault();"
              " openModal(b.dataset.mt,b.dataset.mm||'',function(){bypass=true;"
              " e.target.requestSubmit(b);});});"
              // AlerteInformation (remplace les alert() natifs de la calibration).
              "function say(t,b){openModal(t,b||'');}"
              "</script>");
    html += F("</div></body></html>");
    return html;
}

// --- Handlers ----------------------------------------------------------------

// Applique les champs PRÉSENTS dans la requête, et rien d'autre. Renvoie un
// message d'erreur (vide si OK).
//
// Les 4 onglets ont chacun LEUR formulaire, donc l'onglet Réseau n'envoie que
// wifi_ssid/wifi_pass et l'onglet MQTT que les champs broker : sur une page
// unique, « absent du formulaire » = « ne pas toucher ». Sans cette distinction,
// enregistrer depuis l'onglet Réseau renvoyait « Port MQTT invalide » (port vide
// -> 0) et aurait vidé le broker, et réciproquement pour le SSID.
//
// `s` doit contenir la config DÉJÀ stockée : on part de ce qui est en NVS et on
// n'écrase que ce qui est soumis. L'onglet affiché au retour est celui d'où l'on
// vient (le bandeau de résultat doit être lu à côté des champs concernés).
//
// N'ÉCRIT RIEN : remplit `s` et `silo` (has_silo = le formulaire contient le
// silo). C'est l'appelant qui décide d'enregistrer — le test, lui, jamais.
//
// Les mots de passe enregistrés ne sont pas renvoyés dans la page : un champ
// vide garde l'ancien tant que le réseau (ou l'utilisateur MQTT) est le même.
static String parseForm(Settings& s, SiloConfig& silo, bool& has_silo) {
    has_silo = false;
    if (g_server.hasArg("wifi_ssid")) {
        const String ssid = g_server.arg("wifi_ssid");
        const String pass = g_server.arg("wifi_pass");
        const bool same_net = (ssid == s.wifi_ssid);
        snprintf(s.wifi_ssid, sizeof(s.wifi_ssid), "%s", ssid.c_str());
        if (pass.length() || !same_net)
            snprintf(s.wifi_pass, sizeof(s.wifi_pass), "%s", pass.c_str());
        if (s.wifi_ssid[0] == '\0') return "SSID WiFi vide.";
    }

    if (g_server.hasArg("mqtt_port")) {
        const long port = g_server.arg("mqtt_port").toInt();
        if (port < 1 || port > 65535) return "Port MQTT invalide (1-65535).";
        s.mqtt_port = (uint16_t)port;
    }
    if (g_server.hasArg("mqtt_server")) {
        snprintf(s.mqtt_server, sizeof(s.mqtt_server), "%s",
                 g_server.arg("mqtt_server").c_str());
        if (s.mqtt_server[0] == '\0' || s.mqtt_base[0] == '\0')
            return "Serveur MQTT et base des topics sont obligatoires.";
    }
    if (g_server.hasArg("mqtt_base")) {
        snprintf(s.mqtt_base, sizeof(s.mqtt_base), "%s",
                 g_server.arg("mqtt_base").c_str());
        if (s.mqtt_server[0] == '\0' || s.mqtt_base[0] == '\0')
            return "Serveur MQTT et base des topics sont obligatoires.";
    }

    // L'authentification n'estInterpretée que si le formulaire MQTT a été
    // soumis. Une case NON cochée n'est pas envoyée du tout : impossible de la
    // distinguer d'un formulaire qui ne-parle pas d'authentification. Le champ
    // caché `mqtt_form` (présent dans le seul formulaire MQTT) lève l'ambiguïté.
    if (g_server.hasArg("mqtt_form")) {
        if (g_server.hasArg("mqtt_auth") && g_server.arg("mqtt_user").length() > 0) {
            const String user = g_server.arg("mqtt_user");
            const String pass = g_server.arg("mqtt_pass");
            const bool same_user = (user == s.mqtt_user);
            snprintf(s.mqtt_user, sizeof(s.mqtt_user), "%s", user.c_str());
            if (pass.length() || !same_user)
                snprintf(s.mqtt_pass, sizeof(s.mqtt_pass), "%s", pass.c_str());
        } else {
            s.mqtt_user[0] = '\0'; // case décochée ou utilisateur vide → sans auth
            s.mqtt_pass[0] = '\0';
        }
    }

    // Les champs du silo sont dans le MÊME formulaire (onglet Réglages) : ils
    // sont enregistrés avec le WiFi et le broker, après le test de connexion.
    silo = settingsGetSilo();
    if (g_server.hasArg("capacity")) {
        silo.capacity_kg = g_server.arg("capacity").toFloat();
        silo.low_kg      = g_server.arg("low").toFloat();
        if (g_server.hasArg("bag")) silo.bag_kg = g_server.arg("bag").toFloat();
        if (silo.capacity_kg <= 0.0f)  return "Capacité du silo invalide (> 0 kg).";
        if (silo.low_kg < 0.0f || silo.low_kg > silo.capacity_kg)
            return "Seuil « niveau bas » hors bornes (0 .. capacité).";
        if (silo.bag_kg < 1.0f || silo.bag_kg > 50.0f)
            return "Poids d'un sac hors bornes (1 .. 50 kg).";
        has_silo = true;
    }

    if (g_server.hasArg("aspiration")) {
        uint16_t a = 0;
        if (!parseHhmm(g_server.arg("aspiration"), a))
            return "Heure d'aspiration invalide (HH:MM, 00h00 à 23h59).";
        s.aspiration_min = a;
    }

    // Tolère un copier-coller d'URL dans le champ serveur ("http://…") : on ne
    // garde que l'hôte, sinon PubSubClient tenterait de le résoudre en DNS.
    settingsNormalize(s);
    return "";
}

// Connexion WiFi STA avec la config fournie (utilisée par l'enregistrement et
// par le test). Laisse la STA connectée en cas de succès.
static bool tryWifi(const Settings& s) {
    Serial.printf("[portal] WiFi '%s'...\n", s.wifi_ssid);
    WiFi.mode(WIFI_AP_STA);
    WiFi.disconnect(false);
    WiFi.begin(s.wifi_ssid, s.wifi_pass);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - t0) < kStaTestTimeoutMs) {
        delay(100);
    }
    bool ok = (WiFi.status() == WL_CONNECTED);
    Serial.printf("[portal] WiFi %s\n",
                  ok ? WiFi.localIP().toString().c_str() : "ECHEC");
    return ok;
}

static void handleRoot() {
    g_last_activity = millis();
    g_server.send(200, "text/html; charset=utf-8",
                  pagePortal(settingsGet(), bannerFromQuery()));
}

// Redessiner l'e-paper avec le DERNIER poids connu : sert à vérifier l'affichage
// (une dalle qui s'efface, un bandeau bloqué) sans attendre le cycle du soir.
// Ce n'est PAS une mesure : le poids affiché est celui déjà mesuré.
static void handleEpdRefresh() {
    g_last_activity = millis();

    const bool have = g_state.has_last_measure;
    if (have) {
        displayShowMain(g_state.last_measure_kg, getBatteryPercentage(), isCharging());
    } else {
        displayShowNomade(getBatteryPercentage(), isCharging());
    }
    Serial.printf("[portal] ecran redessine (%s)\n",
                  have ? "ecran principal" : "mode nomade (aucune mesure)");
    g_epd_test_at = millis();   // la boucle du portail remettra son écran
    if (g_epd_test_at == 0) g_epd_test_at = 1;
    g_server.sendHeader("Location", "/?t=etat&ok=epd", true);
    g_server.send(302, "text/plain", "");
}

// Rescanner les réseaux. Le scan coupe le canal de l'AP le temps de l'opération
// (~2-4 s) : le téléphone perd brièvement la connexion, d'où le bouton manuel
// plutôt qu'un scan automatique à chaque chargement de page.
static void handleScan() {
    g_last_activity = millis();
    scanWifiNetworks();
    g_server.sendHeader("Location", "/?t=reglages&ok=scan", true);
    g_server.send(302, "text/plain", "");
}

// --- Calibration -------------------------------------------------------------

// Sensors initialisés une fois (paresseux) : coûteux (~2 s d'attente « ready »),
// donc réservé à la page de calibration, jamais à la page de config.
static bool g_sensors_ready = false;

static void ensureSensorsReady() {
    if (g_sensors_ready) return;
    sensorsInit();
    g_sensors_ready = true;
}

static void handleCal() {
    g_last_activity = millis();
    g_last_activity = millis();
    g_server.send(200, "text/html; charset=utf-8", pagePortal(settingsGet(), ""));
}

// Relevés des 4 pieds : 4 lignes « raw kg » (ou « - - » si indisponible).
// Texte simple volontairement (pas de JSON) : c'est lu par le JS du navigateur.
// ⚠️ Met à jour l'activité du portail : sinon le sondage périodique n'empêcherait
// pas le timeout de 5 min pendant qu'on calibre.
static void handleCalReadings() {
    g_last_activity = millis();
    ensureSensorsReady();

    String out;
    for (uint8_t i = 0; i < 4; i++) {
        float raw = 0.0f, kg = 0.0f;
        // 2 lectures par pied et par relevé : lisse le bruit (un relevé à 1
        // échantillon faisait « sautiller » le Δ), tout en restant compatible
        // avec le sondage périodique (serveur mono-thread).
        if (sensorsReadFoot(i, raw, kg, 2)) {
            out += String(raw, 1) + " " + String(kg, 2);
        } else {
            out += "- -";
        }
        out += "\n";
    }
    g_server.send(200, "text/plain; charset=utf-8", out);
}

// Enregistre la calibration : NVS + application IMMÉDIATE (pas de redémarrage),
// pour que les relevés affichent le résultat tout de suite.
// Aucun test WiFi : la calibration doit marcher sur un banc sans routeur.
static void handleCalSave() {
    g_last_activity = millis();

    Calibration cal = settingsGetCal();
    for (uint8_t i = 0; i < 4; i++) {
        char kz[8], kc[8];
        snprintf(kz, sizeof(kz), "z%u", i);
        snprintf(kc, sizeof(kc), "c%u", i);
        if (!g_server.hasArg(kz) || !g_server.hasArg(kc)) {
            g_server.send(400, "text/html; charset=utf-8",
                          "<meta charset=utf-8><p>Champs manquants. "
                          "<a href=/cal>Retour</a></p>");
            return;
        }
        const float z = g_server.arg(kz).toFloat();
        const float c = g_server.arg(kc).toFloat();
        // Un facteur d'échelle nul rendrait le pied inutilisable (division par
        // zéro dans get_units) : on refuse plutôt que d'enregistrer.
        if (c == 0.0f) {
            char msg[128];   // le message complet fait ~80 car. : 64 tronquait le lien
            snprintf(msg, sizeof(msg),
                     "<meta charset=utf-8><p>Facteur C nul pour le pied %u. "
                     "<a href=/cal>Retour</a></p>", i + 1);
            g_server.send(400, "text/html; charset=utf-8", msg);
            return;
        }
        cal.z[i] = z;
        cal.c[i] = c;
    }

    bool ok = settingsSaveCal(cal);
    if (ok) sensorsApplyCalibration(); // effet immédiat sur les relevés

    String banner = ok
        ? F("<div class=ok><b>Calibration enregistree.</b> Les relevés ci-dessous "
            "utilisent d&eacute;j&agrave; les nouvelles valeurs.</div>")
        : F("<div class=ko><b>Echec de l'ecriture en NVS.</b> Rien n'a "
            "&eacute;t&eacute; enregistr&eacute;.</div>");
    g_server.send(200, "text/html; charset=utf-8",
                  pagePortal(settingsGet(), banner));
}

static void handleSave() {
    g_last_activity = millis();

    // On part de la config ENREGISTRÉE et on n'écrase que les champs soumis :
    // chaque onglet n'envoie que son propre lot (cf. parseForm).
    Settings s = settingsGet();
    SiloConfig silo;
    bool has_silo = false;
    String err = parseForm(s, silo, has_silo);
    if (err.length()) {
        g_server.send(400, "text/html; charset=utf-8",
                      "<meta charset=utf-8><p>" + err +
                          " <a href=/>Retour</a></p>");
        return;
    }

    // Test de connexion STA avant d'enregistrer : évite d'écrire une config
    // qui rendrait la balance injoignable (pas de portail en sortie de veille).
    if (!tryWifi(s)) {
        Serial.println("[portal] WiFi KO, rien n'est enregistré");
        // On reste en AP_STA (repasser en AP couperait brièvement le client) :
        // le portail reste joignable pour réessayer.
        WiFi.disconnect(false);
        String banner = F("<div class=ko><b>Connexion WiFi impossible</b><br>"
                          "Identifiants refuses ou reseau hors portee. "
                          "Rien n'a ete enregistre.</div>");
        g_form_silo = &silo;
        g_server.send(200, "text/html; charset=utf-8",
                  pagePortal(s, banner));
        g_form_silo = nullptr;
        return;
    }

    if (has_silo) settingsSaveSilo(silo);
    settingsSave(s);

    g_server.send(200, "text/html; charset=utf-8",
                  pagePortal(s, F("<div class=ok><b>Réglages enregistrés.</b> "
                                  "La carte redémarre.</div>")));
    delay(800);
    ESP.restart();
}

// Pendant le test (~15-20 s : WiFi jusqu'à 12 s + MQTT jusqu'à 5 s), le portail
// reste figé et l'e-paper n'affiche RIEN : le verdict est renvoyé dans la page
// web, que l'utilisateur est en train de lire. L'ancien écran texte « TEST »
// (FreeMono, 10 car./ligne, sans en-tête ni pied) est supprimé avec le reste de
// l'ancien style — un refresh full de plus pour une information déjà à l'écran.

// Test des paramètres SAISIS (non enregistrés) : WiFi puis MQTT. Le formulaire
// est réaffiché avec le verdict, pour corriger et retester (ou enregistrer).
static void handleTest() {
    g_last_activity = millis();

    // On part de la config ENREGISTRÉE et on n'écrase que les champs soumis :
    // chaque onglet n'envoie que son propre lot (cf. parseForm).
    // Rien n'est enregistré ici : ni le réseau, ni le broker, ni le silo.
    Settings s = settingsGet();
    SiloConfig silo;
    bool has_silo = false;
    String err = parseForm(s, silo, has_silo);
    if (err.length()) {
        g_server.send(400, "text/html; charset=utf-8",
                      "<meta charset=utf-8><p>" + err +
                          " <a href=/>Retour</a></p>");
        return;
    }

    String banner;
    if (!tryWifi(s)) {
        banner = F("<div class=ko><b>WiFi : echec</b><br>"
                   "Identifiants refuses ou reseau hors portee.</div>");
    } else {
        if (mqttTestConnection(s.mqtt_server, s.mqtt_port, s.mqtt_user,
                               s.mqtt_pass)) {
            banner = "<div class=ok><b>WiFi : OK</b> (IP " +
                     WiFi.localIP().toString() + ")<br><b>MQTT : connecte</b> a " +
                     String(s.mqtt_server) + ":" + String(s.mqtt_port) +
                     (s.mqtt_user[0] ? " (avec authentification)" : " (sans authentification)") +
                     "</div>";
        } else {
            banner = "<div class=ko><b>WiFi : OK</b><br><b>MQTT : echec</b> sur " +
                     String(s.mqtt_server) + ":" + String(s.mqtt_port) + ".<br>"
                     "Verifier l'adresse, le port, et l'authentification "
                     "(utilisateur/mot de passe) si le broker en exige une.</div>";
        }
    }

    // Retour à l'écran du portail (nom de l'AP + adresse) pour la suite.
    displayShowConfigPortal(g_ap_ssid, g_ap_pass, settingsHasStored(),
                            getBatteryPercentage(), isCharging());

    // La page réaffiche les valeurs SAISIES (silo compris), pas la NVS.
    g_form_silo = has_silo ? &silo : nullptr;
    g_server.send(200, "text/html; charset=utf-8",
                  pagePortal(s, banner));
    g_form_silo = nullptr;
}

// --- Portail ----------------------------------------------------------------

void webConfigRun() {
    Serial.println("=== Portail de configuration ===");

    // Nom d'AP dérivé de la MAC (dernier mot), comme le client id MQTT.
    uint64_t mac = ESP.getEfuseMac();
    // Nom d'AP court (« Scale-A1B2 ») : il doit tenir sur UNE ligne de l'écran
    // de configuration (10 car. max en 9pt sur 122 px).
    snprintf(g_ap_ssid, sizeof(g_ap_ssid), "Scale-%04X", (uint16_t)(mac & 0xFFFF));
    Serial.printf("[portal] AP '%s'\n", g_ap_ssid);

    // AP_STA : le point d'accès reste actif ET la station est disponible pour
    // scanner les réseaux alentour (le scan n'est pas possible en mode AP seul).
    WiFi.persistent(false);
    // Même hostname que le firmware nominal : le netif STA du portail (test WiFi
    // / enregistrement) s'annonce ainsi sous le même nom.
    WiFi.setHostname(WIFI_HOSTNAME);
    WiFi.mode(WIFI_AP_STA);
    // Mot de passe WPA2 tiré à chaque ouverture et affiché sur l'e-paper : il
    // faut être devant la balance pour rejoindre le portail.
    snprintf(g_ap_pass, sizeof(g_ap_pass), "%08lu",
             (unsigned long)(esp_random() % 100000000UL));
    WiFi.softAP(g_ap_ssid, g_ap_pass);

    IPAddress ip = WiFi.softAPIP();
    Serial.printf("[portal] IP %s\n", ip.toString().c_str());

    // Redirection DNS : toute résolution pointe vers le portail (déclenche
    // l'ouverture automatique de la page sur la plupart des téléphones).
    g_dns.start(53, "*", ip);

    // Écran du mode : titre « PORTAIL ACTIF » + réseau AP, mot de passe, adresse.
    displayShowConfigPortal(g_ap_ssid, g_ap_pass, settingsHasStored(),
                            getBatteryPercentage(), isCharging());

    // Scan des réseaux (bloquant, quelques secondes) : mis en cache pour que la
    // page propose les SSID dans une liste déroulante.
    scanWifiNetworks();

    g_server.on("/", HTTP_GET, handleRoot);
    g_server.on("/save", HTTP_POST, handleSave);
    g_server.on("/test", HTTP_POST, handleTest);
    g_server.on("/epd",  HTTP_POST, handleEpdRefresh);
    g_server.on("/scan", HTTP_GET, handleScan);
    g_server.on("/cal", HTTP_GET, handleCal);
    g_server.on("/cal/readings", HTTP_GET, handleCalReadings);
    g_server.on("/cal/save", HTTP_POST, handleCalSave);
    g_server.onNotFound([]() {
        g_last_activity = millis();
        g_server.sendHeader("Location", "http://192.168.4.1/", true);
        g_server.send(302, "text/plain", "");
    });
    g_server.begin();

    // Le pied de l'écran annonce « ● COURT REDEMARRER » : on cable l'appui court.
    // La capture des gestes n'est PAS gratuite — l'ISR de timer échantillonne à
    // 1 kHz et consomme du courant — donc elle n'est active que le temps du
    // portail, et coupée juste avant le redémarrage.
    //
    // Le bouton peut être ENCORE MAINTENU à cet instant : `appPortalConfirm()`
    // lance le portail dès que la barre de maintien est pleine, sans attendre le
    // relâchement. L'ISR verrait alors ce relâchement comme un appui long, et le
    // portail quitterait dans la seconde — un portail qui s'ouvre et se referme
    // tout seul. On attend donc le relâchement AVANT d'armer la capture (et
    // buttonEnable() remet les compteurs à zéro : rien de parasite ne reste).
    if (readRefillButton()) {
        Serial.println("[portal] bouton encore tenu, attente du relâchement");
        const uint32_t t_release = millis();
        while (readRefillButton() && (millis() - t_release) < 5000) delay(20);
    }
    buttonEnable(true);

    g_last_activity = millis();
    while ((millis() - g_last_activity) < kPortalTimeoutMs) {
        g_dns.processNextRequest();
        g_server.handleClient();

        // Sortie demandée : le portail est une fonction TERMINALE (elle ne rend
        // jamais la main — `app.cpp` l'appelle sans tester le retour), donc
        // « sortir » ne peut pas revenir au menu : c'est un redémarrage, comme
        // le timeout. L'appui LONG est accepté aussi (par habitude du maintien sur
        // tous les autres écrans) sans être annoncé : le pied ne promet que le
        // court. Sans config enregistrée, la carte redémarre en portail, ce qui
        // est le comportement voulu (l'AP reste disponible pour la saisie).
        if (buttonTakeShorts() > 0 || buttonTakeLong()) {
            Serial.println("[portal] appui -> sortie");
            break;
        }

        // Fin du test d'affichage : l'écran du portail revient (pied, mot de
        // passe). Non bloquant : la page web reste servie pendant le test.
        if (g_epd_test_at && (millis() - g_epd_test_at) >= kEpdTestShowMs) {
            g_epd_test_at = 0;
            displayShowConfigPortal(g_ap_ssid, g_ap_pass, settingsHasStored(),
                                    getBatteryPercentage(), isCharging());
        }
        delay(2);
    }

    buttonEnable(false);   // avant la sortie : la carte peut deep-sleeper

    if ((millis() - g_last_activity) >= kPortalTimeoutMs)
        Serial.println("[portal] timeout");

    // Carte SANS configuration : on rend la main, l'appelant la met en veille
    // (réveil batterie seul, le bouton rouvre le portail). Redémarrer relancerait
    // le portail indéfiniment, AP allumé, sans garde batterie.
    if (!settingsHasStored()) {
        Serial.println("[portal] aucune config enregistree -> veille");
        g_server.stop();
        g_dns.stop();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);
        return;
    }
    Serial.println("[portal] redemarrage");
    ESP.restart();
}
