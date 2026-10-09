#pragma once

#include <Arduino.h>

// =============================================================================
// GESTION DE L'ALIMENTATION ET DU DEEP SLEEP
// =============================================================================

// Cause du réveil, déterminée par powerGetWakeCause().
enum class WakeCause : uint8_t {
    TIMER,      // réveil programmé (créneau autour de l'aspiration, ou durée spéciale)
    GPIO,       // réveil par GPIO (bouton GPIO39, redock, ...)
    ULP,        // réveil ULP (veille nomade) : redock, ou CHRG a changé
    COLD_BOOT   // reset / 1er boot / power-on (pas un vrai réveil)
};

/**
 * @brief Prépare le système pour le deep sleep.
 * Arme le wakeup par timer (toujours : c'est le réveil de secours) ET laisse
 * les wakes GPIO configurés ailleurs actifs. Balaie les GPIO inutilisés
 * (registre pins.hpp).
 * @param sleep_seconds Durée de sleep en secondes
 */
void prepareDeepSleep(uint32_t sleep_seconds);

/**
 * @brief Entre en deep sleep. Ne revient JAMAIS.
 */
void enterDeepSleep();

/**
 * @brief Libère le maintien des PD_SCK (HX711) posé par prepareDeepSleep() et
 * rend les pads RTC au chemin numérique (y compris la pad DOUT de réveil ext0,
 * basculée en RTC IO par un réveil dedock/redock).
 *
 * À appeler AVANT de piloter SCK : un pad maintenu garde son niveau figé,
 * l'impulsion basse de power_up() serait sans effet (HX711 reste éteint).
 */
void powerReleaseSensorHold();

/**
 * @brief Cause du dernier réveil (esp_sleep_get_wakeup_cause).
 */
WakeCause powerGetWakeCause();

/**
 * @brief Mesure la tension batterie (pont diviseur GPIO 35).
 * @return Tension en volts
 */
float readBatteryVoltage();

/**
 * @brief Niveau batterie en pourcentage (linéaire 3.3V→4.2V).
 * @return Pourcentage (0-100)
 */
uint8_t getBatteryPercentage();

/**
 * @brief Détecte la charge USB via le pin CHRG du TP4054.
 * @return true si en charge
 */
bool isCharging();

/**
 * @brief Arme les réveils de la veille nomade sur le coprocesseur ULP, à la
 * place d'ext0 (l'IDF refuse ULP + ext0). Toutes les 200 ms l'ULP lit :
 *  - la ligne DOUT du pied 1 : HAUTE = redock → réveil immédiat ;
 *  - CHRG : réveil quand le niveau diffère de `charging_shown` pendant 1 s
 *    (anti-rebond).
 * @param charging_shown état de charge affiché (l'éclair) : un écart avec la
 *        réalité au moment du sommeil réveille donc la carte, qui se corrige.
 */
void armNomadeUlpWakeup(bool charging_shown);

/**
 * @brief Arrête l'ULP (hors veille nomade : aucun coût, aucun réveil).
 */
void disarmNomadeUlpWakeup();

/**
 * @brief Après un réveil ULP : true si c'est la ligne DOUT (redock), false si
 * c'est CHRG.
 */
bool ulpWokeForRedock();

// --- Interaction GPIO (bouton remplissage, contact de présence boîtier) -------

/**
 * @brief Arme un réveil sur appui du bouton de remplissage (ext1, état docké).
 * GPIO 39 input-only → pull-up externe requis.
 */
void armRefillWakeup();

/**
 * @brief Arme un réveil ext0 sur la ligne DOUT au niveau BAS (dédock, état docké).
 * La ligne DOUT doit être chargée par la pull-down externe ~1 MΩ
 * (pins::DOCK_WAKE_DOUT) : HX711 alimenté → HIGH, ligne ouverte → LOW.
 */
void armDedockWakeup();

/**
 * @brief Arme un réveil ext0 sur la ligne DOUT au niveau HAUT (redock, dédocké).
 */
void armRedockWakeup();

/**
 * @brief true si le bouton de remplissage est appuyé (lecture directe).
 */
bool readRefillButton();

/**
 * @brief true si le dernier réveil vient d'ext0 (ligne DOUT dedock/redock).
 */
bool dockWakeupExt0();

/**
 * @brief true si la ligne DOUT est BASSE = boîtier retiré (dédock confirmé).
 */
bool readDockSignal();

/**
 * @brief Bitmask des GPIO qui ont déclenché le réveil ext1 (0 si autre cause).
 */
uint64_t powerGetExt1Status();

/**
 * @brief true si le réveil ext1 vient du bouton de remplissage.
 */
bool refillButtonWoke();

// --- Alimentation écran (T5 V2.4, GPIO 12 = EPD_PWR_EN) -----------------------
// No-op tant que USE_EPD_PWR_CUTOFF == 0 (config.hpp).

/**
 * @brief Coupe l'alimentation de l'écran et maintient le niveau pendant le
 * deep sleep (domaine RTC). À appeler juste avant prepareDeepSleep().
 * La RAM de trame du SSD1680 ne survit PAS à la coupure : le prochain
 * displayPanelWake() doit être en FULL (voir canPartial(), display.cpp).
 */
void epdPowerCut();

/**
 * @brief Restaure l'alimentation de l'écran (libère le hold RTC posé par
 * epdPowerCut(), puis HIGH). À appeler avant tout accès SPI à l'écran
 * (display.init()), au réveil comme au premier boot.
 */
void epdPowerRestore();
