#include "config.hpp"
#include "power.hpp"
#include "pins.hpp"

#include <esp_sleep.h>
#include <esp_wifi.h>
#include <esp_bt.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp32/ulp.h>
#include <soc/rtc_cntl_reg.h>
#include <soc/rtc_io_reg.h>
#include <SPI.h>

// =============================================================================
// MAINTIEN D'UN PAD PENDANT LE DEEP SLEEP
// =============================================================================
// Deux mécanismes distincts, selon le domaine du pad :
//
//  - pads NUMÉRIQUES : gpio_hold_en() → registre RTC_IO_DIG_PAD_HOLD. La table
//    GPIO_HOLD_MASK de l'IDF vaut 0 pour les pads RTC (0,2,4,12-15,25-27,32-39)
//    et pour 34-39 : y appeler gpio_hold_en() ne fait RIEN (no-op SILENCIEUX).
//    Le verrou n'agit en deep sleep que combiné à gpio_deep_sleep_hold_en().
//
//  - pads RTC : le verrou SEUL (rtc_gpio_hold_en) ne suffit pas. Il fige un
//    état côté RTC alors que la pad est pilotée par la MATRICE NUMÉRIQUE, qui
//    s'éteint au sommeil : le niveau posé disparaît avant d'être tenu. Mesuré
//    sur la carte : SCK oscillant (2,2-3,3 V) et HX711 rallumés → plusieurs mA.
//    Il faut basculer la pad SOUS le domaine RTC (rtc_gpio_init) et l'y laisser
//    en sortie haute : c'est le domaine RTC — alimenté toute la nuit — qui
//    pilote la ligne. rtc_gpio_hold_en() ne fait plus que verrouiller cette
//    configuration (output enable + valeur + fonction + drive strength).
static void holdPad(gpio_num_t pin, uint32_t level) {
    if (rtc_gpio_is_valid_gpio(pin)) {
        rtc_gpio_init(pin);
        rtc_gpio_set_direction(pin, RTC_GPIO_MODE_OUTPUT_ONLY);
        rtc_gpio_set_drive_capability(pin, GPIO_DRIVE_CAP_3);
        rtc_gpio_set_level(pin, level);
        rtc_gpio_hold_en(pin);
    } else {
        gpio_hold_en(pin);
    }
}

static void releasePad(gpio_num_t pin) {
    if (rtc_gpio_is_valid_gpio(pin)) {
        rtc_gpio_hold_dis(pin);
        // Rend la pad au chemin numérique : sans ça, pinMode()/digitalWrite()
        // n'ont plus prise sur elle et l'HX711 ne peut plus être piloté.
        rtc_gpio_deinit(pin);
    } else {
        gpio_hold_dis(pin);
    }
}

// Libère le maintien des 4 PD_SCK posé par prepareDeepSleep() et rend les pads
// RTC au chemin numérique. À appeler AVANT de piloter SCK (power_up) : une pad
// maintenue garde son niveau figé, l'impulsion basse serait sans effet.
void powerReleaseSensorHold() {
    gpio_deep_sleep_hold_dis();
    const gpio_num_t sck_pins[4] = { pins::LC1_SCK, pins::LC2_SCK,
                                     pins::LC3_SCK, pins::LC4_SCK };
    for (int i = 0; i < 4; i++) {
        releasePad(sck_pins[i]);
    }
    // Réveil ext0 : la pad DOUT de réveil a été basculée en RTC IO par le
    // contrôleur de réveil. La rendre au chemin numérique, sinon le pied 1
    // n'est plus lisible (rtc_gpio_deinit() documenté par l'IDF après ext0).
    releasePad(pins::DOCK_WAKE_DOUT);
}

// =============================================================================
// IMPLEMENTATION
// =============================================================================

void prepareDeepSleep(uint32_t sleep_seconds) {
    Serial.println("Preparing for deep sleep...");

    // Wakeup par timer (secours), toujours armé.
    esp_sleep_enable_timer_wakeup((uint64_t)sleep_seconds * 1000000ULL);

    // Eteint le WiFi et le Bluetooth (déjà fait dans wifi.cpp, mais on assure).
    esp_wifi_stop();
    esp_bt_controller_disable();

    // --- Balayage des GPIO inutilisés -----------------------------------
    // Toute broche déclarée dans pins.hpp (registre unique) est épargnée.
    // Les autres passent en input + pulldown pour minimiser la consommation.
    // Exclusions structurelles (jamais pilotables) :
    //  - flash SPI 6-11, UART0 1/3, GPIO inexistants 20-24/28-31
    const gpio_num_t used_pins[] = {
        pins::EPD_BUSY, pins::EPD_CS, pins::EPD_RST, pins::EPD_DC,
        pins::EPD_SCK, pins::EPD_MOSI,
        pins::BAT_ADC, pins::CHRG_DETECT,
        pins::LC1_DOUT, pins::LC1_SCK,
        pins::LC2_DOUT, pins::LC2_SCK,
        pins::LC3_DOUT, pins::LC3_SCK,
        pins::LC4_DOUT, pins::LC4_SCK,
        pins::BTN_REFILL,
        pins::EPD_PWR_EN,       // config posée par epdPowerCut() (T5 V2.4) : ne pas toucher
        pins::BOOT_BTN          // ne pas tirer bas en sleep (mode download)
    };

    for (int i = 0; i < 40; i++) {
        if (i >= 6 && i <= 11) continue;                              // flash SPI
        if (i == 1 || i == 3) continue;                               // UART0
        if ((i >= 20 && i <= 24) || (i >= 28 && i <= 31)) continue;   // inexistants

        bool used = false;
        for (gpio_num_t p : used_pins) {
            if ((int)p == i) { used = true; break; }
        }
        if (used) continue;

        gpio_set_direction((gpio_num_t)i, GPIO_MODE_INPUT);
        gpio_set_pull_mode((gpio_num_t)i, GPIO_PULLDOWN_ONLY);
    }

    // --- HX711 en power-down pendant le sleep ---------------------------
    // PD_SCK maintenu HIGH (>60 µs après la transition bas→haut) → le HX711
    // reste éteint ; DOUT en input SANS pull-up (~290 µA économisés).
    const gpio_num_t dout_pins[4] = { pins::LC1_DOUT, pins::LC2_DOUT,
                                      pins::LC3_DOUT, pins::LC4_DOUT };
    const gpio_num_t sck_pins[4]  = { pins::LC1_SCK,  pins::LC2_SCK,
                                      pins::LC3_SCK,  pins::LC4_SCK };
    for (int i = 0; i < 4; i++) {
        gpio_set_direction(dout_pins[i], GPIO_MODE_INPUT);
        gpio_pullup_dis(dout_pins[i]);  // pas de pull-up en sleep
        gpio_set_direction(sck_pins[i], GPIO_MODE_OUTPUT);
        gpio_set_level(sck_pins[i], 1);
    }

    // MAINTIEN des SCK : le niveau haut est la SEULE chose qui garde les HX711
    // en power-down, et le domaine numérique s'éteint en deep sleep → sans
    // maintien le pad retombe, les HX711 se rallument (~3 mA par module :
    // puce + pont de jauges) et la veille passe de ~250 µA à ~12 mA.
    // L'API dépend du pad : nos SCK sont 32/27 (RTC) et 21/19 (numériques).
    for (int i = 0; i < 4; i++) {
        holdPad(sck_pins[i], 1);
    }
    gpio_deep_sleep_hold_en();  // arme l'auto-hold des pads numériques

    // e-paper (T5 V2.4) : alimentation coupée par GPIO 12 et lignes CS/DC/RST/
    // SCK/MOSI tenues LOW, posé par epdPowerCut() (displayPanelDeepSleep()).
}

void enterDeepSleep() {
    Serial.println("Entering deep sleep...");
    Serial.flush();
    delay(100); // Attend que le Serial soit vide

    esp_deep_sleep_start();

    // Ne devrait jamais être atteint.
    while (1) {
        delay(1000);
    }
}

// ---------------------------------------------------------------------------
// Causes de réveil
// ---------------------------------------------------------------------------

WakeCause powerGetWakeCause() {
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    switch (cause) {
        case ESP_SLEEP_WAKEUP_TIMER: return WakeCause::TIMER;
        case ESP_SLEEP_WAKEUP_EXT0:
        case ESP_SLEEP_WAKEUP_EXT1:  return WakeCause::GPIO;
        case ESP_SLEEP_WAKEUP_ULP:   return WakeCause::ULP;
        default:                     return WakeCause::COLD_BOOT;
    }
}

// ---------------------------------------------------------------------------
// Batterie
// ---------------------------------------------------------------------------

float readBatteryVoltage() {
    // Lecture ADC sur le pont diviseur intégré (TTGO T5, GPIO 35).
    // analogReadMilliVolts() applique la calibration eFuse de la puce (l'ADC de
    // l'ESP32 est non linéaire : l'ancien raw/4095×3,3 lisait ~0,25 V trop bas
    // vers 4 V) ; la moyenne lisse le bruit.
    uint32_t mv = 0;
    for (int i = 0; i < 8; i++) mv += analogReadMilliVolts((uint8_t)pins::BAT_ADC);
    return (mv / 8) / 1000.0f * BATTERY_VOLTAGE_RATIO;
}

uint8_t getBatteryPercentage() {
    float voltage = readBatteryVoltage();

    // Plage utile : 4.2V (batterie pleine) → 3.3V (seuil d'arrêt).
    const float V_FULL = 4.2f;
    const float V_EMPTY = BATTERY_SHUTDOWN_THRESHOLD_V;

    if (voltage >= V_FULL) return 100;
    if (voltage <= V_EMPTY) return 0;
    return (uint8_t)(((voltage - V_EMPTY) / (V_FULL - V_EMPTY)) * 100.0f);
}

bool isCharging() {
    // Pin CHRG du TP4054, drain ouvert, pull-up EXTERNE 10 kΩ vers +3V3 →
    // HIGH = pas en charge, LOW = en charge. armNomadeUlpWakeup() a pu laisser
    // la pad en RTC IO : on la rend au GPIO, sinon digitalRead() ne la voit plus.
    rtc_gpio_deinit(pins::CHRG_DETECT);
    pinMode((uint8_t)pins::CHRG_DETECT, INPUT);
    return (digitalRead((uint8_t)pins::CHRG_DETECT) == LOW);
}

// ---------------------------------------------------------------------------
// Réveils de la veille nomade (coprocesseur ULP : redock + CHRG)
// ---------------------------------------------------------------------------
// ext1 (bouton, ALL_LOW) ne sait pas réveiller sur « bouton bas OU CHRG bas »,
// et l'IDF refuse ULP + ext0 (« Conflicting wake-up trigger »). En veille
// nomade, le coprocesseur ULP remplace donc ext0 : toutes les kUlpPeriodUs il
// lit
//  - la ligne DOUT du pied 1 : HAUTE = boîtier reposé → réveil immédiat (le
//    cycle qui suit confirme par la mesure, comme avec ext0) ;
//  - CHRG : réveil quand le niveau diffère de celui attendu sur
//    kUlpDebounceSamples lectures consécutives (anti-rebond).
// Après un réveil, I_END arrête le timer de l'ULP : il ne repart que réarmé,
// au sommeil suivant.
//
// Mémoire RTC lente (mots de 32 bits, l'ULP n'en voit que les 16 bits bas) :
//   [0] niveau attendu de CHRG (1 = pas en charge, 0 = en charge)
//   [1] lectures consécutives différentes
//   [2…] programme
static const uint32_t kUlpVarExpected = 0;
static const uint32_t kUlpVarCount    = 1;
static const uint32_t kUlpProgAddr    = 2;
static const uint32_t kUlpPeriodUs    = 200000;   // 200 ms (~400 ms mesurés)
static const uint16_t kUlpDebounceSamples = 5;    // ~1-2 s

void armNomadeUlpWakeup(bool charging_shown) {
    const gpio_num_t chrg = pins::CHRG_DETECT;
    const gpio_num_t dout = pins::DOCK_WAKE_DOUT;

    // Pads en RTC IO, lisibles par l'ULP, SANS pull interne : CHRG a sa pull-up
    // externe 10 kΩ vers +3V3 (GPIO 34 n'en a de toute façon pas), DOUT sa
    // pull-down externe 1 MΩ (comme pour ext0). RTC_PERIPH reste alimenté
    // pour que l'ULP lise les pads.
    rtc_gpio_init(chrg);
    rtc_gpio_set_direction(chrg, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_init(dout);
    rtc_gpio_set_direction(dout, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_dis(dout);
    rtc_gpio_pulldown_dis(dout);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    const uint32_t chrg_bit = RTC_GPIO_IN_NEXT_S + rtc_io_number_get(chrg);
    const uint32_t dout_bit = RTC_GPIO_IN_NEXT_S + rtc_io_number_get(dout);
    const ulp_insn_t program[] = {
        I_MOVI(R3, kUlpVarExpected),          // R3 = adresse des variables
        I_RD_REG(RTC_GPIO_IN_REG, dout_bit, dout_bit), // R0 = ligne DOUT
        M_BGE(3, 1),                          // HAUTE = redock → réveil
        I_RD_REG(RTC_GPIO_IN_REG, chrg_bit, chrg_bit), // R0 = niveau de CHRG
        I_LD(R1, R3, kUlpVarExpected),        // R1 = niveau attendu
        I_SUBR(R0, R0, R1),
        M_BXZ(1),                             // identique → compteur à zéro
        I_LD(R0, R3, kUlpVarCount),           // différent : compteur + 1
        I_ADDI(R0, R0, 1),
        I_ST(R0, R3, kUlpVarCount),
        M_BL(2, kUlpDebounceSamples),         // pas encore confirmé → on s'arrête
        M_LABEL(3),
        I_WAKE(),                             // redock / CHRG confirmé : réveil
        I_END(),                              // et arrêt du timer de l'ULP
        I_HALT(),
        M_LABEL(1),
        I_MOVI(R0, 0),
        I_ST(R0, R3, kUlpVarCount),
        M_LABEL(2),
        I_HALT(),
    };

    RTC_SLOW_MEM[kUlpVarExpected] = charging_shown ? 0 : 1;
    RTC_SLOW_MEM[kUlpVarCount]    = 0;
    size_t size = sizeof(program) / sizeof(ulp_insn_t);
    esp_err_t err = ulp_process_macros_and_load(kUlpProgAddr, program, &size);
    if (err == ESP_OK) err = ulp_set_wakeup_period(0, kUlpPeriodUs);
    if (err == ESP_OK) err = esp_sleep_enable_ulp_wakeup();
    if (err == ESP_OK) err = ulp_run(kUlpProgAddr);
    if (err != ESP_OK) {
        // Sans ULP, plus de réveil au redock ni de charge : le timer reste
        // (00:02 / 5 min) — on le dit, c'est un défaut à corriger.
        Serial.printf("ULP nomade: ECHEC (%d), redock vu au timer seulement\n", err);
        return;
    }
    Serial.printf("ULP nomade: arme (redock + CHRG, attendu %s ; lus DOUT=%d CHRG=%d)\n",
                  charging_shown ? "en charge" : "pas en charge",
                  rtc_gpio_get_level(dout), rtc_gpio_get_level(chrg));
}

void disarmNomadeUlpWakeup() {
    // Même effet que l'I_END du programme : plus aucun réveil de l'ULP.
    CLEAR_PERI_REG_MASK(RTC_CNTL_STATE0_REG, RTC_CNTL_ULP_CP_SLP_TIMER_EN);
}

bool ulpWokeForRedock() {
    // La pad DOUT est encore en RTC IO (armNomadeUlpWakeup) : lecture côté RTC.
    // sensorsInit() la rendra au GPIO (powerReleaseSensorHold).
    return rtc_gpio_get_level(pins::DOCK_WAKE_DOUT) == 1;
}

// ---------------------------------------------------------------------------
// Interaction GPIO (bouton remplissage, ligne DOUT dedock/redock)
// ---------------------------------------------------------------------------
// Bouton : contact NO → GND, pull-up EXTERNE (GPIO 39 input-only). Repos =
// HIGH, appui = LOW. Armé en ext1 sur niveau BAS.
//
// Ligne DOUT (ext0) : le DOUT du pied 1 est chargé par une pull-down externe
// ~1 MΩ. HX711 alimenté + power-down (SCK tenu haut) → DOUT HIGH (docké) ;
// pogo ouvert → ligne flottante → LOW (dédocké). On arme ext0 sur le niveau
// OPPOSÉ à l'état courant — sinon un niveau déjà « actif » au moment du sleep
// provoquerait un réveil immédiat en boucle :
//   - DOCKED   → ext0 sur LOW  (réveil au dédock) + ext1 bouton
//   - DEDOCKED → ext0 sur HIGH (réveil au redock) + ext1 bouton (re-test)
//
// ext0 et ext1 sont indépendants et COMBINABLES (doc ESP-IDF) : ext0 pilote un
// GPIO RTC unique (la ligne DOUT), ext1 le bouton (masque). ext0 garde le
// domaine RTC_PERIPH alimenté pendant le sleep (léger surcoût de veille).

// Prépare la pad DOUT pour un réveil ext0 : aucune pull interne (le niveau est
// défini par la pull-down externe ~1 MΩ, plus économe qu'une pull RTC).
static void configureDockWakeupPin() {
    rtc_gpio_pullup_dis(pins::DOCK_WAKE_DOUT);
    rtc_gpio_pulldown_dis(pins::DOCK_WAKE_DOUT);
}

// Bouton de remplissage : arme un réveil sur appui (état docké).
void armRefillWakeup() {
    gpio_set_direction(pins::BTN_REFILL, GPIO_MODE_INPUT);
    // GPIO 39 input-only → pull-up externe requis (sinon flottant).
    esp_sleep_enable_ext1_wakeup(1ULL << (uint64_t)pins::BTN_REFILL,
                                 ESP_EXT1_WAKEUP_ALL_LOW);
}

// Ligne DOUT : arme un réveil ext0 sur passage BAS = dédock (état docké).
void armDedockWakeup() {
    configureDockWakeupPin();
    esp_sleep_enable_ext0_wakeup(pins::DOCK_WAKE_DOUT, 0);
}

// Ligne DOUT : arme un réveil ext0 sur passage HAUT = redock (état dédocké).
void armRedockWakeup() {
    configureDockWakeupPin();
    esp_sleep_enable_ext0_wakeup(pins::DOCK_WAKE_DOUT, 1);
}

// Lecture de l'état du bouton (true = appuyé). Anti-rebond côté appelant.
bool readRefillButton() {
    return (gpio_get_level(pins::BTN_REFILL) == 0);
}

// Broche(s) qui ont déclenché le réveil ext1 (bitmask). 0 si pas un réveil ext1.
uint64_t powerGetExt1Status() {
    return esp_sleep_get_ext1_wakeup_status();
}

// true si le réveil ext1 vient du bouton de remplissage.
bool refillButtonWoke() {
    return (powerGetExt1Status() & (1ULL << (uint64_t)pins::BTN_REFILL)) != 0;
}

// true si le dernier réveil vient d'ext0 (ligne DOUT dedock/redock).
bool dockWakeupExt0() {
    return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0;
}

// Lecture de la ligne DOUT (true = LOW = boîtier retiré). Les pulls internes
// sont coupées pour laisser la pull-down externe fixer le niveau quand la ligne
// est ouverte (sinon la pull-up de lecture, activée par sensorsInit, masquerait
// le dédock).
bool readDockSignal() {
    gpio_set_direction(pins::DOCK_WAKE_DOUT, GPIO_MODE_INPUT);
    gpio_pullup_dis(pins::DOCK_WAKE_DOUT);
    gpio_pulldown_dis(pins::DOCK_WAKE_DOUT);
    return (gpio_get_level(pins::DOCK_WAKE_DOUT) == 0);
}

// ---------------------------------------------------------------------------
// Alimentation écran (T5 V2.4, GPIO 12 = EPD_PWR_EN)
// ---------------------------------------------------------------------------
// Remplace le hibernate() logiciel + pull-ups 100 kΩ CS/RST de la V2.3.1 par
// une coupure matérielle totale de la LDO écran. GPIO 12 est RTC (holdPad()
// prend le chemin rtc_gpio, même mécanisme que les PD_SCK des HX711) : le
// niveau posé ici survit réellement au deep sleep, contrairement à un
// gpio_set_level() nu sur une pad non-RTC (cf. note sur CS/RST dans
// prepareDeepSleep()).

#if USE_EPD_PWR_CUTOFF
// GPIO 12 démarre en fonction JTAG (MTDI) : gpio_set_direction() seul ne la
// bascule pas en GPIO et le niveau posé n'atteint jamais la broche.
// gpio_config() sélectionne la fonction GPIO dans l'IO_MUX.
static void epdPowerPinAsOutput() {
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << (uint64_t)pins::EPD_PWR_EN;
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
}

// Lignes pilotées par l'ESP vers la dalle. Laissées HIGH pendant la coupure,
// elles réalimentent la dalle par ses diodes de protection (le hold global
// gpio_deep_sleep_hold_en() fige leur dernier état) : on les tient LOW.
static const gpio_num_t kEpdOutPins[] = {
    pins::EPD_CS, pins::EPD_DC, pins::EPD_RST, pins::EPD_SCK, pins::EPD_MOSI
};
#endif

void epdPowerCut() {
#if USE_EPD_PWR_CUTOFF
    SPI.end(); // rend SCK/MOSI au GPIO ; GxEPD2 refait SPI.begin() à l'init
    for (gpio_num_t p : kEpdOutPins) {
        gpio_reset_pin(p);
        gpio_set_pull_mode(p, GPIO_FLOATING);
        gpio_set_direction(p, GPIO_MODE_OUTPUT);
        gpio_set_level(p, 0);
        gpio_hold_en(p);
    }
    gpio_reset_pin(pins::EPD_BUSY);
    gpio_set_pull_mode(pins::EPD_BUSY, GPIO_FLOATING);

    epdPowerPinAsOutput();
    gpio_set_level(pins::EPD_PWR_EN, 0); // LOW = coupé (HIGH = actif, cf. pins.hpp)
    // LOW est aussi le niveau sûr pour ce strapping (MTDI) au réveil.
    holdPad(pins::EPD_PWR_EN, 0);
    // gpio_deep_sleep_hold_en() global est (re)posé par prepareDeepSleep().
#endif
}

void epdPowerRestore() {
#if USE_EPD_PWR_CUTOFF
    releasePad(pins::EPD_PWR_EN); // lève le hold RTC posé par epdPowerCut()
    for (gpio_num_t p : kEpdOutPins) gpio_hold_dis(p);
    epdPowerPinAsOutput();
    gpio_set_level(pins::EPD_PWR_EN, 1); // HIGH = alimentation active
    delay(10); // stabilisation LDO avant tout accès SPI — à ajuster à la mesure
#endif
}
