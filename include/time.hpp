#pragma once

#include <Arduino.h>
#include <time.h>

// =============================================================================
// GESTION DU TEMPS (NTP + RTC)
// =============================================================================

/**
 * @brief Initialise le NTP et synchronise l'heure
 *
 * Tente une synchro FRAÎCHE à chaque appel (pas de court-circuit sur une heure
 * RTC valide : le RTC survit au deep sleep mais dérive). À appeler AVANT tout
 * affichage d'horloge — sinon l'écran montre l'heure RTC dérivée.
 *
 * @param ntpServer Serveur NTP (ex: "time.nist.gov")
 * @param timezone Fuseau horaire (ex: "CET-1CEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00")
 * @param timeout_sec Timeout en secondes
 * @return true si le SNTP a répondu (heure corrigée) ; false si échec (l'heure
 *         RTC, locale, est alors conservée telle quelle)
 */
bool timeInit(const char* ntpServer = "time.nist.gov", 
             const char* timezone = "CET-1CEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00",
             uint16_t timeout_sec = 10);

/**
 * @brief Configure le fuseau horaire (configTzTime) sans attendre la synchro.
 * À appeler au boot AVANT tout affichage : le fuseau doit être actif même sans
 * WiFi (sinon l'heure RTC s'affiche en UTC/GMT). Démarre aussi le SNTP.
 */
void timeConfigureTimezone();

/**
 * @brief Obtient le timestamp Unix
 * @return Timestamp en secondes
 */
time_t timeGetTimestamp();

/**
 * @brief Vérifie si le RTC est valide
 * @return true si le RTC a une date plausible
 */
bool timeIsValid();
