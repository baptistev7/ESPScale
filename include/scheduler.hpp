#pragma once

#include <cstdint>

#include "state.hpp" // ScaleMode

// =============================================================================
// ORDONNANCEUR DE REVEIL (créneaux encadrant l'aspiration)
// =============================================================================
// Objectif métier : des BORNES DE CONSO claires, prises de part et d'autre de
// l'aspiration quotidienne (réglable dans le portail, `Settings::aspiration_min`) :
//   - 30 min AVANT : niveau juste avant l'aspiration. Isole les mouvements de la
//     journée (notamment les remplissages) de la conso de la nuit → un appoint
//     est vu comme une hausse AVANT l'aspiration, pas mélangé au creux nocturne.
//     Filet de sécurité contre le masquage d'une conso.
//   - 30 min APRÈS : capture le niveau juste après l'aspiration → conso de la
//     nuit.
// Chaque intervalle entre deux réveils = une fenêtre de conso propre.
//
// Avec l'aspiration par défaut de 18h30, on retrouve exactement les réveils
// historiques de 18h00 et 19h00 : les deux fenêtres mesurent ce qu'elles
// mesuraient, mais l'aspiration peut être déplacée (travail tardif, aspiration
// programmable) sans toucher au code.

// Décalage des deux réveils autour de l'aspiration, en minutes.
constexpr uint16_t kWakeOffsetMin = 30;

// Compensation de dérive RTC (secondes ajoutées au sleep).
constexpr uint32_t kRtcDriftCompensationSec = 15;

// --- Veille nomade (boîtier retiré de sa base) ---
// Le redock réveille la carte tout seul (ligne DOUT, USE_DOCK_WAKE_DOUT) : le
// timer ne sert plus qu'à tenir l'écran à jour. Hors charge, la carte dort
// jusqu'au PREMIER de ces deux instants :
//   - 00:02 : la date de l'en-tête change (2 min de marge contre la dérive du
//     RTC ; réveillée avant minuit, elle se rerendort jusqu'à 00:02). Ce réveil
//     fait le cycle complet (re-test des capteurs = filet du redock, garde
//     batterie, synchro NTP si > 24 h) et redessine l'écran ;
//   - le temps estimé pour perdre 1/4 de la batterie au courant de veille
//     (~94 j : en pratique, c'est toujours minuit qui gagne).
// En charge (USB), réveil toutes les kNomadeChargePollSec pour montrer la
// montée de la jauge — c'est l'USB qui paie.
constexpr uint32_t kBatteryUsableMah      = 1800;  // 2000 mAh, arrêt à 3,3 V
constexpr uint32_t kNomadeSleepCurrentUa  = 200;   // 192 µA mesurés à vide
constexpr uint32_t kNomadeQuarterSec =
    (uint32_t)((uint64_t)kBatteryUsableMah * 1000ULL / 4ULL * 3600ULL /
               kNomadeSleepCurrentUa);                    // ≈ 94 j
constexpr uint16_t kNomadeDayStartMin     = 2;              // 00:02
constexpr uint32_t kNomadeChargePollSec   = 5UL * 60UL;     // 5 min
// Horloge inconnue (coupure d'alimentation, pas de WiFi) : minuit n'a pas de
// sens, on retente la synchro toutes les heures.
constexpr uint32_t kNomadeNoClockSec      = 3600UL;         // 1 h

// --- Durées spéciales (secondes) ---
constexpr uint32_t kEmergencySleepSec   = 24UL * 3600UL; // batterie très basse : 24 h
constexpr uint32_t kCriticalSleepSec    = 7UL * 24UL * 3600UL; // batterie critique : 1 sem
// Carte SANS configuration réseau, portail refermé : aucun cycle de mesure
// possible. Elle dort et ne se réveille que pour vérifier sa batterie (le bouton
// rouvre le portail).
constexpr uint32_t kUnconfiguredSleepSec = 24UL * 3600UL; // 24 h
// Timeout d'un refill interrompu par un dedock (refill_pending) : si le silo
// n'est pas reposé sous 1 h, le refill est annulé (retour DOCKED).
constexpr uint32_t kRefillPendingSec    = 1UL * 3600UL; // 1 h

// --- Seuils ---
constexpr uint8_t  kRefillPriceDigits   = 3;    // x,xx €

/**
 * @brief Les deux créneaux de réveil, en minutes depuis minuit (0..1439).
 * @param i 0 = 30 min avant l'aspiration, 1 = 30 min après.
 * Lecalcul gère le passage de minuit : une aspiration à 00h15 donne 23h45 et
 * 00h45. Utilise l'aspiration ENREGISTRÉE (jamais une constante) — c'est la
 * seule façon pour l'écran et le portail d'afficher les mêmes heures que celles
 * que l'ordonnanceur va réellement servir.
 */
uint16_t schedulerWakeSlotMin(uint8_t i);

/**
 * @brief Idem, mais pour une heure d'aspiration DONNÉE (calcul pur, sans NVS).
 * C'est ce que l'interface utilise : le récap sous le champ doit refléter ce
 * qui est saisi, pas ce qui est encore en mémoire — sinon le formulaire annonce
 * « 09:15 » juste au-dessus d'un récap « 18h00 / 19h00 ».
 */
uint16_t schedulerWakeSlotFrom(uint16_t aspiration_min, uint8_t i);

/**
 * @brief Secondes jusqu'au PROCHAIN créneau, + compensation dérive.
 * Si l'heure RTC est invalide, retombe sur une période fixe (12 h).
 */
uint32_t schedulerSecondsUntilNextWake();

/**
 * @brief Durée de sommeil adaptée au mode courant.
 * DOCKED/DEGRADED  → prochain créneau (aspiration ±30 min).
 * DEDOCKED         → en charge : kNomadeChargePollSec ; sinon jusqu'à 00:02
 *                    (ou 1/4 de batterie, si plus tôt) ; horloge inconnue : 1 h.
 *                    Le redock, lui, réveille la carte par la ligne DOUT.
 * REFILL_POURING/PRI → court (30 min) — l'utilisateur revient appuyer.
 */
uint32_t schedulerSleepSeconds(ScaleMode mode);
