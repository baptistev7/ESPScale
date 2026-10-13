#pragma once

#include "config.hpp"

#include <Arduino.h>

// =============================================================================
// PUBLICATION MQTT
// =============================================================================

/**
 * @brief Connecte le client MQTT au broker configuré.
 * @param timeout_sec Deadline GLOBALE de connexion en secondes
 * @return true si connecté (nécessite le WiFi actif au préalable)
 */
bool mqttConnect(uint16_t timeout_sec = MQTT_TIMEOUT_SEC);

/**
 * @brief Teste une connexion MQTT avec des paramètres NON enregistrés (portail).
 * N'utilise pas la configuration NVS : sert au bouton « tester » du portail,
 * avant enregistrement. Se déconnecte à la fin.
 * @param server Hôte / IP du broker
 * @param port   Port du broker
 * @param user   Utilisateur (nullptr ou "" = sans authentification)
 * @param pass   Mot de passe (idem)
 * @param timeout_sec Deadline globale
 * @return true si la connexion a abouti
 */
bool mqttTestConnection(const char* server, uint16_t port,
                        const char* user, const char* pass,
                        uint16_t timeout_sec = MQTT_TIMEOUT_SEC);

/**
 * @brief Publie le poids (entier, kg) sur "<base>/value" ("450", retain)
 */
bool mqttPublishWeight(float weight);

/**
 * @brief Publie le niveau batterie sur "<base>/battery" ("87", retain)
 */
bool mqttPublishBattery(uint8_t percent);

/**
 * @brief Publie la version du firmware sur "<base>/version" (FW_VERSION, retain)
 */
bool mqttPublishVersion();

/**
 * @brief Publie l'état d'erreur capteurs sur "<base>/error/on-off" ("on"/"off")
 */
bool mqttPublishError(bool error);

/**
 * @brief Publie la qualité de mesure sur "<base>/quality" (JSON)
 * {"valid":N,"mode":"ok|degraded|dedock|error"}
 */
bool mqttPublishQuality(uint8_t valid_count, const char* mode);

/**
 * @brief Publie l'état du cycle de remplissage sur "<base>/refill/state"
 * @param state "docked" | "armed" | "pricing" | "error"
 */
bool mqttPublishRefillState(const char* state);

/**
 * @brief Publie l'événement de remplissage validé sur "<base>/refill/event"
 * (JSON, PAS de retain). Source de vérité comptable côté HA.
 * @param added_kg       Poids ajouté MESURÉ (kg) — pour le stock
 * @param bag_price_eur  Prix d'un sac saisi (€)
 * @param bag_count      Nombre de sacs versés (le coût = bag_count × prix)
 */
bool mqttPublishRefillEvent(float added_kg, float bag_price_eur, uint8_t bag_count);

/**
 * @brief Déconnecte le client MQTT
 */
void mqttDisconnect();
