#pragma once

#include <cstdint>

// =============================================================================
// CONFIGURATION GENERALE
// =============================================================================

// Version du firmware — publiée sur le topic "<base>/version" (retain) pour
// tracer quelle version tourne sur la carte.
#define FW_VERSION "1.0.0"

// -----------------------------------------------------------------------------
// Réseau et broker — valeurs compilées : seulement des DÉFAUTS de formulaire
// -----------------------------------------------------------------------------
// Le WiFi (SSID/mot de passe) et le serveur MQTT ne sont PAS définis dans le
// code : ils vivent uniquement en NVS, saisis via le portail de configuration
// (voir settings.hpp et webconfig.hpp). Une carte sans configuration enregistrée
// démarre donc directement dans le portail.
//
// Deux constantes restent, comme valeurs initiales du formulaire (elles ne sont
// pas propres au site et restent modifiables dans le portail) :
//   - MQTT_PORT : port standard du protocole ;
//   - MQTT_TOPIC_BASE_DEFAULT : base des topics par défaut ("scale").
//
// Les topics sont construits à l'exécution : "<base>/value", "<base>/battery",
// "<base>/version", "<base>/error/on-off", "<base>/quality",
// "<base>/refill/state", "<base>/refill/event".
constexpr uint16_t MQTT_PORT = 1883;
#define MQTT_TOPIC_BASE_DEFAULT "scale"
#define MQTT_TIMEOUT_SEC 5

// Hostname réseau de la carte, annoncé en DHCP. Sans lui, le core arduino-esp32
// envoie son défaut "esp32-<3 derniers octets MAC>". Ce n'est pas un réglage de
// site : c'est l'identité fixe de l'appareil.
#define WIFI_HOSTNAME "pellet-scale"

// --- Dédoublonnage des publications ---
// La dernière valeur envoyée est mémorisée en RAM RTC (survit au deep sleep).
// On ne se réveille pas sur le réseau tant que rien n'a changé.
// Re-publication si |Δpoids| >= ce seuil (kg)
#define MQTT_SEND_DELTA_KG 1.0f
// ... ou si |Δbatterie| >= ce seuil (%)
#define MQTT_BATTERY_DELTA_PCT 5
// ... ou tous les N cycles sans envoi (heartbeat, retain rafraîchi après
// redémarrage du broker). 2 réveils/jour → 8 cycles ≈ 4 jours.
#define MQTT_HEARTBEAT_CYCLES 8

// =============================================================================
// CONFIGURATION MATERIELLE
// =============================================================================

// --- Broches ---
// Le registre des GPIO utilisés vit dans pins.hpp (source de vérité unique).
// Le sweep deep sleep et le code applicatif le consultent.

// Réveil immédiat dedock/redock via la ligne DOUT d'un pied (ext0) + pull-down
// externe ~1 MΩ côté ESP (voir pins.hpp::DOCK_WAKE_DOUT). Un HX711 alimenté en
// power-down pilote DOUT HIGH (docké), la ligne ouverte tombe LOW (dédocké).
// ⚠ Requiert la pull-down : sans elle, une ligne flottante en DEDOCKED peut
// provoquer une boucle de réveil. Tant que 0 : dédock vu au réveil timer et
// redock au poll complet de la veille nomade (≤ 15 min).
// Résistance posée (V2.4) : activé.
#define USE_DOCK_WAKE_DOUT 1

// Réveil sur changement de CHRG (début / fin de charge) en veille nomade : le
// coprocesseur ULP lit la broche toutes les 200 ms pendant le deep sleep et
// réveille la carte quand elle a changé depuis 1 s (voir armNomadeUlpWakeup()).
// L'IDF refuse ULP + ext0 : en nomade, c'est donc AUSSI l'ULP qui surveille la
// ligne DOUT (redock) à la place d'ext0. Exige CHRG sur une broche RTC
// (pins.hpp : GPIO 34, pull-up externe 10 kΩ).
// Surchargeable à la compilation (mesure de conso comparative, sans ULP) :
//   PLATFORMIO_BUILD_FLAGS="-DUSE_CHRG_ULP=0" pio run
#ifndef USE_CHRG_ULP
#define USE_CHRG_ULP 1
#endif

// Coupure matérielle de l'alimentation de l'écran (T5 V2.4 uniquement :
// GPIO 12 = EPD_PWR_EN, absent sur la V2.3.1). Remplace le hibernate()
// logiciel + pull-ups 100 kΩ CS/RST de la V2.3.1 par une coupure totale de
// la LDO écran pendant le deep sleep. Contrepartie : la RAM de trame du
// SSD1680 NE SURVIT PLUS au sommeil -> un FULL refresh est forcé au premier
// draw de chaque réveil (voir canPartial(), display.cpp).
// Polarité VALIDÉE sur banc (V2.4) : LOW = écran coupé, HIGH = écran alimenté
// (~1 mA au PPK). NB : la coupure supprime la fuite de la dalle mais PAS le
// plancher de la carte -> deep sleep mesuré 192 µA à vide (écran branché ou non,
// cf. issue LilyGO #66 : « 30 µA » seulement avec une modif matérielle de
// l'EN du LDO U6).
#define USE_EPD_PWR_CUTOFF 1

// Ratio du pont diviseur batterie intégré au TTGO T5
// Appliqué à la tension de la broche en mV calibrée (analogReadMilliVolts).
// Valeur de départ (2 × 100 kΩ) à recaler sur la V2.4 au PPK2 : voir
// docs/CONSOMMATION.md#lecture-de-la-batterie.
#define BATTERY_VOLTAGE_RATIO 2.0f

// --- Protection batterie basse tension ---
// Le T5 n'a PAS de circuit de déconnexion basse tension : sans protection
// logicielle, la carte tire sur la batterie jusqu'au brownout (~2.6-2.8V),
// ce qui endommage une Li-ion. Seuils conservateurs :
//  - < BATTERY_SHUTDOWN_THRESHOLD_V : cycle sauté (pas de WiFi/affichage),
//    on se rendort immédiatement pour ne pas solliciter la batterie.
//  - < BATTERY_EMERGENCY_THRESHOLD_V : mode urgence, sommeil prolongé
//    (BATTERY_EMERGENCY_SLEEP_H) pour laisser la batterie récupérer.
#define BATTERY_SHUTDOWN_THRESHOLD_V 3.3f
#define BATTERY_EMERGENCY_THRESHOLD_V 3.2f
#define BATTERY_EMERGENCY_SLEEP_H 24
// Nb de réveils consécutifs sous le seuil d'urgence avant le sommeil très long
// (1 semaine). Évite de rester en mode urgence indéfiniment.
#define BATTERY_EMERGENCY_CYCLES 3

// Réservoir plein (en kg) — contenu NOMINAL, pas la limite de mesure.
#define TANK_FULL_KG 670.0

// Seuil d'alerte « niveau bas » (%) : au-dessous, l'écran principal affiche le
// bandeau inversé « NIVEAU BAS » + « [ REMPLISSAGE ] ». Provisoire en attendant
// le réglage en NVS (portail).
#define SILO_LOW_ALERT_PCT 20

// Capacité PHYSIQUE de mesure — filtre de plausibilité.
// Chaque pied = 4 cellules de 50 kg sommées = 200 kg ; au-delà, le pied sature
// et sa valeur ne veut plus rien dire. Le poids lu par pied est une PART de la
// charge qu'il porte : il ne peut donc jamais dépasser la capacité du pied.
// Sert à rejeter une lecture physiquement impossible (pied surchargé) au lieu
// de la publier comme une hausse légitime (→ faux « refill sauvage » ≥ 14 kg).
#define FOOT_CAPACITY_KG 200.0f
#define TANK_CAPACITY_KG (FOOT_CAPACITY_KG * 4.0f) // 800 kg (4 pieds)

// Poids nominal d'un sac de granulés (kg) — repère pour juger le poids mesuré.
// Le coût, lui, se base sur le nombre de sacs déclarés × prix du sac.
//
// DEFAUT de formulaire : le poids du sac varie selon le fournisseur et
// l'humidité du granulé, c'est donc un réglage d'INSTALLATION. Il est modifiable
// dans le portail (onglet REGLAGES, « Poids d'un sac ») et stocké en NVS
// (`SiloConfig::bag_kg`) ; cette constante n'est plus utilisée qu'au premier
// démarrage, tant que rien n'a été enregistré.
#define BAG_KG 15

// --- Capteurs HX711 : facteurs d'étalonnage par pied ---
// (les broches DOUT/SCK sont dans pins.hpp)
// Foot 1
#define Z_FACTOR_1 -498735.0f
#define C_FACTOR_1 -20690.0f

// Foot 2
#define Z_FACTOR_2 92822.0f
#define C_FACTOR_2 -20690.0f

// Foot 3
#define Z_FACTOR_3 -818867.0f
#define C_FACTOR_3 -21230.0f

// Foot 4
#define Z_FACTOR_4 -826467.0f
#define C_FACTOR_4 -19600.0f

// =============================================================================
// CONFIGURATION AUTONOMIE
// =============================================================================

// Timeout WiFi en secondes
#define WIFI_CONNECT_TIMEOUT_SEC 15

// Durée d'inactivité avant que le portail de configuration ne redémarre la
// carte (il garde l'ESP éveillé en AP : coûteux en batterie). Affichée sur
// l'écran du portail (« ARRÊT AUTO : N MIN ») — le webconfig et l'affichage
// partagent cette constante pour rester exacts.
#define PORTAL_TIMEOUT_MIN 10

// Nombre de tentatives de lecture des capteurs
// (lecture parallèle des 4 capteurs par tentative : 2 × 200 ms ≈ 400 ms)
#define SENSOR_READ_ATTEMPTS 2

// Delay entre tentatives (ms)
#define SENSOR_READ_DELAY_MS 200

// Timeout global d'attente que TOUS les HX711 soient prêts à l'init (ms)
#define SENSOR_READY_TIMEOUT_MS 2000

// =============================================================================
// CONFIGURATION AFFICHAGE
// =============================================================================

#include <GxEPD2_BW.h>
#include "epd_panel.hpp" // EpdPanel : GDEY + LUT partiel du 213_BN
// (les polices Adafruit FreeMono* ne sont plus incluses : tous les écrans sont
// rendus avec les polices bitmap de la charte, cf. include/fonts/ ci-dessous)

// Polices dérivées des TTF de la charte UI (docs/UI) : JetBrains Mono ExtraBold
// (texte technique) et Oswald Bold (poids), générées avec fontconvert.
// Plage 0x20-0xFF (accents) pour JbmXb, 0x20-0x5A pour Oswald.
#include "fonts/JbmXb4.h"       // h~7 px : état, libellés jauge, secondaire, pied
#include "fonts/JbmXb5.h"       // h~8 px : date de l'en-tête
#include "fonts/JbmXb6.h"       // h~9 px : bandeau d'alerte
#include "fonts/OswaldBold10.h" // h~16 px : unité (KG)
#include "fonts/OswaldBold30.h" // h~49 px : valeur principale

#define MAX_DISPLAY_BUFFER_SIZE 65536ul
#define GxEPD2_DISPLAY_CLASS GxEPD2_BW
// EpdPanel = GDEY0213B74 (full refresh rapide, ~2,6 s) + la mise à jour
// partielle du 213_BN (le GDEY n'a pas de LUT partiel → « partiel » plein écran).
// Le 213_BN seul dépasse le busy_timeout GxEPD2 de 10 s en full → grésillement.
// Un SSD1680 laissé dans un état douteux est resynchronisé par le reset manuel
// appuyé de displayWakeup().
#define GxEPD2_DRIVER_CLASS EpdPanel

#define MAX_HEIGHT(EPD) (EPD::HEIGHT <= MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8) ? EPD::HEIGHT : MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8))
