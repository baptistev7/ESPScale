#pragma once

#include <Arduino.h>

// =============================================================================
// ETAT PERSISTANT (RAM RTC)
// =============================================================================
// État unique du système, conservé en RAM RTC (survit au deep sleep,
// réinitialisé à froid / power-on). Remplace les anciens statics RTC épars
// (rtc_has_last, rtc_error_state, ...) par une structure typée unique, avec
// des transitions explicites.

// Mode courant de la balance. Pilote le comportement du cycle (mesure,
// publication, affichage) et le calcul du prochain sommeil.
enum class ScaleMode : uint8_t {
    DOCKED,        // boîtier posé sur sa base, mesure OK (cycle normal)
    DEDOCKED,      // boîtier retiré (0 capteur / contact pogo ouvert)
    DEGRADED,      // 1-3 capteurs valides (strict 4/4 non atteint)
    REFILL_POURING,  // remplissage : l'utilisateur verse (poids en direct, puis sacs)
    REFILL_PRICING // remplissage : saisie du prix en cours
};

// Phase du refill quand il est interrompu par un dedock (voir refill_pending).
enum class RefillPhase : uint8_t {
    NONE = 0,       // pas de refill en attente
    BEFORE_MEASURE, // dedock avant la mesure "avant" (prix saisi)
    AFTER_MEASURE   // dedock avant la mesure "après" (sacs versés)
};

// Écran actuellement affiché par la dalle e-paper. Persiste en RAM RTC : sert à
// décider, au réveil suivant, si une zone peut être rafraîchie en partial (même
// écran déjà affiché) ou s'il faut un full refresh.
enum class DisplayScreen : uint8_t {
    UNKNOWN = 0, // cold boot : on ne sait pas ce qui est affiché -> full
    MAIN,        // écran principal (silo-jauge + poids)
    DEDOCK,      // écran boîtier retiré (dédock confirmé, silo endormi)
    REFILL_LIVE, // ECRAN11 — poids versé en direct (étape 1/3)
    REFILL_BAGS, // ECRAN12 — nombre de sacs (étape 2/3)
    REFILL_PRICE,// ECRAN13 — prix du sac, saisi par digits (étape 3/3)
    REFILL_CONFIRM, // ECRAN14 — « TERMINER ? », le maintien SAUVE
    REFILL_SAVED,   // ECRAN15 — « ENREGISTRÉ » (récap, 1 min)
    CONFIG,      // écran du portail de configuration
    OPTIONS,     // menu OPTIONS (liste + ligne sélectionnée inversée)
    INFORMATIONS, // écran d'informations, 2 pages (état général / répartition)
    PORTAL_CONFIRM, // confirmation de démarrage du portail (maintien = lance)
    UNCONFIGURED,  // carte sans configuration en veille (« CARTE EN VEILLE »)
    REFILL_FIX     // « CORRIGER ? » (court sur ECRAN14) : sacs / prix / annuler
};

// État persistant complet (une seule variable RTC_DATA_ATTR).
struct ScaleState {
    // Garde-fou : si la structure change (layout), les données sont invalidées.
    uint32_t magic;

    // Mode courant
    ScaleMode mode;

    // Dédoublonnage des publications MQTT
    bool     has_last_weight;   // une valeur déjà envoyée ?
    float    last_weight_kg;
    uint8_t  last_battery_pct;
    bool     last_send_ok;      // dernier envoi réussi ?
    uint32_t skip_count;        // cycles sans publication (heartbeat)

    // Remplissage en cours
    float    refill_before_kg;  // mesure "avant" (kg)
    float    refill_delta_kg;   // delta mesuré "après - avant" (kg)
    uint8_t  price_digits[3];   // saisie x,xx € — 3 digits
    uint8_t  price_idx;         // digit en cours (0-2)
    uint8_t  refill_bag_count;  // nb de sacs versés (1 clic = +1 pendant POURING)
    time_t   refill_started_at; // timestamp de début du refill (time()) —
                                // sert au timeout d'abandon (2 h)
    // Refill interrompu par un dedock (reprise au redock, timeout 1 h)
    RefillPhase refill_pending; // phase où le refill a été interrompu

    // Batterie basse
    uint8_t  low_batt_count;    // réveils consécutifs sous le seuil d'urgence

    // Dernière mesure VALIDE (4/4) : poids + horodatage. Sert à l'écran
    // « MODE NOMADE » (boîtier hors du dock, aucune mesure possible) pour
    // afficher le dernier poids connu et son ancienneté — jamais une valeur
    // « actuelle ». `last_measure_at` reste 0 si l'heure RTC était invalide.
    bool     has_last_measure;
    float    last_measure_kg;
    time_t   last_measure_at;

    // Mode d'erreur déjà ANNONCÉ (publié + affiché). DOCKED = aucune erreur en
    // cours. Distingue la transition (il faut publier/afficher) de l'état
    // stationnaire (dedock : poll batterie seulement), y compris après le
    // stateSetMode(DOCKED) optimiste du début de cycle.
    ScaleMode announced_error;

    // Affichage : ce que la dalle montre réellement (piloté par display.cpp).
    // Décide entre full refresh et partial refresh ciblé.
    DisplayScreen disp_screen;       // écran affiché (UNKNOWN au cold boot)
    uint8_t       disp_battery;      // % batterie affiché
    bool          disp_charging;     // icône éclair affichée
    uint8_t       disp_partial_run;  // partials consécutifs sans full (anti-ghosting)
    time_t        disp_drawn_at;     // `now` du dernier full de l'écran nomade :
                                     // permet de le redessiner À L'IDENTIQUE au
                                     // réveil (RAM de trame perdue, V2.4)
};

// Instance unique vivant en RAM RTC. `static` interdit ici : doit être visible
// de state.cpp et app.cpp via cet extern.
extern RTC_DATA_ATTR ScaleState g_state;

// Constante de validation (change si le layout de ScaleState change).
constexpr uint32_t STATE_MAGIC = 0x53544159; // "STAY" (layout 6 : horodatage du dessin nomade)

/**
 * @brief Initialise l'état RTC. À appeler au boot (setup).
 * Si le magic ne correspond pas (1er boot / power-on / layout changé),
 * l'état est réinitialisé (stateReset()).
 */
void stateInit();

/**
 * @brief Remet l'état à zéro (mode DOCKED, aucun historique).
 */
void stateReset();

/**
 * @brief Change le mode courant et journalise la transition.
 */
void stateSetMode(ScaleMode new_mode);

/**
 * @brief Renvoie le mode courant.
 */
ScaleMode stateGetMode();

/**
 * @brief Nom court du mode ("docked", "dedocked", "degraded", ...).
 * Utile pour les logs et le topic MQTT scale/refill/state.
 */
const char* stateModeName(ScaleMode mode);
