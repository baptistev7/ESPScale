#pragma once

#include <cstdint>

// =============================================================================
// ENREGISTREUR DE GESTES DU BOUTON (GPIO 39)
// =============================================================================
// Un TIMER MATÉRIEL échantillonne le bouton à 1 kHz dans une ISR : le geste est
// donc capté quelles que soient les activités bloquantes du programme (attente
// BUSY de la dalle ~880 ms, connexion WiFi/MQTT, mesure capteurs). Aucun clic
// n'est perdu, et l'anti-rebond + la classification court/long se font dans
// l'ISR (donc jamais en retard sur les fronts).
//
// Pourquoi pas attachInterrupt() : sur ce core, l'ISR GPIO (service non
// installé) ne s'arme pas de façon fiable sur GPIO 39 → des gestes manquaient.

/**
 * @brief (Dé)active la capture et remet les compteurs à zéro.
 * Activer au début d'une session interactive ; couper avant le deep sleep.
 */
void buttonEnable(bool on);

/**
 * @brief Remet la machine à états et les compteurs à zéro.
 * À utiliser pour JETER l'appui de réveil, une fois qu'il a été relâché.
 */
void buttonReset();

/**
 * @brief Retourne ET remet à zéro le nombre de clics courts en attente.
 * (N clics pendant un blocage = N, pas un seul.)
 */
uint8_t buttonTakeShorts();

/**
 * @brief Retourne ET remet à zéro le flag d'appui long en attente.
 */
bool buttonTakeLong();
