#pragma once

#include <driver/gpio.h>

// =============================================================================
// REGISTRE UNIQUE DES GPIO UTILISES
// =============================================================================
// Source de vérité unique du câblage : toute broche utilisée par le firmware
// est déclarée ICI, et nulle part ailleurs. Le sweep deep sleep (power.cpp)
// et le code applicatif consultent ce registre — fini la liste d'exclusions
// dupliquée entre config.hpp / power.cpp / sensors.cpp.
//
// Référence carte : LilyGO T5 V2.4 (ESP32-D0WDQ6).
// Seuls les GPIO RTC (0,2,4,12-15,25-27,32-39) peuvent réveiller le deep sleep.

namespace pins {

// --- Écran e-paper (SSD1680) ---
constexpr gpio_num_t EPD_BUSY  = GPIO_NUM_4;
constexpr gpio_num_t EPD_CS    = GPIO_NUM_5;
constexpr gpio_num_t EPD_RST   = GPIO_NUM_16;
constexpr gpio_num_t EPD_DC    = GPIO_NUM_17;
constexpr gpio_num_t EPD_SCK   = GPIO_NUM_18; // SPI matériel (VSPI par défaut)
constexpr gpio_num_t EPD_MOSI  = GPIO_NUM_23;

// --- Batterie / charge ---
constexpr gpio_num_t BAT_ADC     = GPIO_NUM_35; // pont diviseur (input-only)
// CHRG du TP4054 (drain ouvert, LOW = en charge) + pull-up EXTERNE 10 kΩ vers
// +3V3 (le 3,3 V de l'ESP, PAS VCC_IO). GPIO 34 = RTC_GPIO 4 : broche RTC
// (lue par l'ULP pendant le deep sleep), entrée seule, sans pull interne.
// Historique : GPIO 19 (non RTC : ne peut pas réveiller la carte), puis 13 et
// 15 — écartées : sur la V2.4 leurs pull-ups 10 kΩ du lecteur SD sont sur
// VCC_IO, que GPIO 12 coupe avec l'écran ; coupées, elles tirent la ligne vers
// 0 V (~0,6 V mesurés, lu « en charge » en permanence).
constexpr gpio_num_t CHRG_DETECT = GPIO_NUM_34;

// --- Capteurs HX711 (4 pieds) ---
// DOUT = data, SCK = horloge (maintenu HIGH en sleep = power-down)
constexpr gpio_num_t LC1_DOUT = GPIO_NUM_25;
constexpr gpio_num_t LC1_SCK  = GPIO_NUM_32;
constexpr gpio_num_t LC2_DOUT = GPIO_NUM_22;
constexpr gpio_num_t LC2_SCK  = GPIO_NUM_21;
constexpr gpio_num_t LC3_DOUT = GPIO_NUM_33;
// Pas GPIO 14 : CLK du lecteur SD, sa pull-up 10 kΩ vers VCC_IO (coupé avec
// l'écran) faisait débiter la SCK haute en veille (+190 à +480 µA, mesuré).
constexpr gpio_num_t LC3_SCK  = GPIO_NUM_19;
constexpr gpio_num_t LC4_DOUT = GPIO_NUM_26;
constexpr gpio_num_t LC4_SCK  = GPIO_NUM_27;

// --- Interaction utilisateur ---
// Bouton de remplissage (Button 1 / P21 sur T5 V2.3.1). GPIO 39 est RTC mais
// INPUT-ONLY : pas de pull interne → pull-up externe requis (repos HIGH,
// appui = LOW). À confirmer au multimètre.
constexpr gpio_num_t BTN_REFILL = GPIO_NUM_39;

// --- Réveil dedock/redock (ligne DOUT d'un pied, sans microswitch) ---
// Le DOUT du pied 1 (GPIO 25, RTC) est ramené sous un pogo et chargé par une
// pull-down externe ~1 MΩ côté ESP. Un HX711 alimenté et en power-down (SCK
// tenu haut) pilote DOUT à HIGH → boîtier posé ; pogo ouvert → ligne flottante
// → LOW → boîtier retiré. Utilisée en ext0 : LOW = dédock, HIGH = redock
// (voir USE_DOCK_WAKE_DOUT dans config.hpp).
constexpr gpio_num_t DOCK_WAKE_DOUT = LC1_DOUT; // GPIO_NUM_25

// --- Alimentation écran (T5 V2.4 uniquement) ---
// GPIO 12 = EPD_PWR_EN, coupure de la LDO écran (absent sur la V2.3.1, où
// cette broche n'a aucune fonction dédiée — cf. exemple officiel LilyGO
// DeepSleep.ino : "EPD_PWR_EN (12) // Only V2.4 Version, v2.3.1 version not
// have this pin"). HIGH = alimentation active (confirmé par cet exemple :
// pinMode(OUTPUT) + digitalWrite(HIGH) avant tout accès SPI à l'écran).
// C'est aussi une broche de STRAPPING (MTDI, sélection tension flash) : ne
// jamais la piloter avant la fin du boot. Utilisée quand USE_EPD_PWR_CUTOFF
// == 1 (config.hpp) ; polarité validée sur banc (HIGH = écran alimenté).
constexpr gpio_num_t EPD_PWR_EN = GPIO_NUM_12;

// --- Broches interdites ---
constexpr gpio_num_t BOOT_BTN = GPIO_NUM_0; // strapping + BOOT : ne pas utiliser

} // namespace pins
