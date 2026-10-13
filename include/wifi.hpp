#pragma once

#include "config.hpp"

#include <Arduino.h>
#include <WiFi.h>

// =============================================================================
// GESTION WIFI OPTIMISEE POUR L'AUTONOMIE
// =============================================================================

/**
 * @brief Initialise la connexion WiFi
 * @param timeout_sec Timeout en secondes
 * @return true si connecté
 */
bool wifiConnect(uint16_t timeout_sec = WIFI_CONNECT_TIMEOUT_SEC);

/**
 * @brief Déconnecte le WiFi et coupe le module
 */
void wifiDisconnect();

/**
 * @brief Vérifie si le WiFi est connecté
 * @return true si connecté
 */
bool wifiIsConnected();

/**
 * @brief Niveau de réception en dBm, 0 si le WiFi est coupé. Convention de
 * l'écran d'informations : 0 = « -- » (pas de mesure).
 */
int16_t wifiRSSI();
