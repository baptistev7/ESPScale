#pragma once

#include <Arduino.h>
#include <cstdint>

// =============================================================================
// CONFIGURATION PERSISTANTE (NVS)
// =============================================================================
// Paramètres WiFi (SSID/mot de passe) et MQTT (serveur, port, base des topics).
// Ils vivent UNIQUEMENT en NVS : aucune valeur n'est compilée dans le firmware.
// Tant que le réseau ET le broker ne sont pas enregistrés, settingsHasStored()
// renvoie false et la carte démarre dans le portail de configuration (voir
// webconfig.hpp). Les autres réglages — calibration des pieds — ont des valeurs
// par défaut : leur absence ne doit surtout pas déclencher le portail.

struct Settings {
    char     wifi_ssid[33];   // SSID (32 max + NUL)
    char     wifi_pass[65];   // mot de passe (64 max + NUL)
    char     mqtt_server[40]; // hôte ou IP du broker
    uint16_t mqtt_port;       // port du broker
    char     mqtt_base[24];   // base des topics (ex "scale")
    char     mqtt_user[33];   // utilisateur MQTT — vide = sans authentification
    char     mqtt_pass[65];   // mot de passe MQTT (idem)

    // Heure d'aspiration quotidienne, en MINUTES depuis minuit (0..1439).
    // Ce n'est pas un réveil : c'est l'instant que la carte SURVEILLE. Les deux
    // réveils (mesures) l'encadrent à ±30 min (cf. scheduler.hpp), parce que
    // deux fenêtres de conso propres — juste avant et juste après — valent mieux
    // qu'une seule mesure prise au hasard. Par défaut 18h30 : c'est l'horaire
    // pour lequel ce firmware a été écrit (mesures à 18h00 et 19h00).
    uint16_t aspiration_min;
};

// =============================================================================
// REGLAGES DU SILO (NVS, CLÉS PROPRES)
// =============================================================================
// Ces valeurs décrivent l'INSTALLATION PHYSIQUE de l'utilisateur, pas son réseau :
// elles sont donc dans un bloc à part, comme la calibration — un enregistrement
// WiFi/MQTT ne doit pas les écraser, et inversement. Elles s'appliquent
// IMMÉDIATEMENT (pas de redémarrage) : elles n'entrent dans aucun chemin réseau.
//
// Tant qu'elles sont absentes, on retombe sur les constantes compilées
// (TANK_FULL_KG, BAG_KG, SILO_LOW_ALERT_PCT) : une carte déjà en
// service garde donc EXACTEMENT le comportement d'aujourd'hui, et le portail n'est
// jamais déclenché par leur absence.
struct SiloConfig {
    float capacity_kg;  // capacité utile du silo (remplissage + charge max)
    float low_kg;       // seuil « niveau bas » (bandeau e-paper), en kg
    float bag_kg;       // poids NOMINAL d'un sac — le repère affiché pendant le
                        // remplissage (« 12 sacs × 15 kg »). Sert à DONNER UNE
                        // ÉCHELLE à l'utilisateur pendant qu'il verse ; le coût,
                        // lui, vient du prix saisi par digits, jamais de ce poids.
};

// =============================================================================
// CALIBRATION DES PIEDS (NVS)
// =============================================================================
// Facteurs d'étalonnage par pied, ajustables depuis le portail (sans recompiler)
// et stockés en NVS SOUS DES CLÉS PROPRES : l'enregistrement de la config WiFi
// ne doit pas les écraser, et inversement. Tant que rien n'a été enregistré, les
// valeurs par défaut sont les constantes compilées (Z_FACTOR_x / C_FACTOR_x).
struct Calibration {
    float z[4]; // offsets (valeur brute qui correspond à 0 kg)
    float c[4]; // facteurs d'échelle (unités brutes par kg)
};

/**
 * @brief Configuration courante (chargée une fois au boot).
 */
const Settings& settingsGet();

/**
 * @brief Charge la configuration depuis la NVS. À appeler dans setup().
 * Absente (carte neuve) : les champs restent vides.
 */
void settingsLoad();

/**
 * @brief true si le réseau ET le broker ont été enregistrés en NVS (portail).
 * C'est la seule condition qui envoie au portail au démarrage : tout le reste a
 * des valeurs par défaut. Une carte dont la calibration est absente mais le
 * réseau configuré est donc opérationnelle.
 */
bool settingsHasStored();

/**
 * @brief Écrit la configuration en NVS.
 * @return true si l'écriture a réussi.
 */
bool settingsSave(const Settings& s);

/**
 * @brief Nettoie les champs qui tolèrent un copier-coller d'URL.
 * Le serveur MQTT doit être un HÔTE seul : on retire le schéma éventuel
 * ("http://", "mqtt://"…), le chemin, et on récupère le port si l'utilisateur
 * l'a collé au serveur ("hote:1883"). Appliqué au chargement, à l'enregistrement
 * et par le portail (bouton de test) — sinon PubSubClient tenterait de résoudre
 * "http://192.168.1.2" en DNS.
 */
void settingsNormalize(Settings& s);

/**
 * @brief Réglages du silo courants (chargés au boot, cache RAM).
 * Défauts = constantes compilées si rien n'a été enregistré.
 */
const SiloConfig& settingsGetSilo();

/**
 * @brief Enregistre les réglages du silo en NVS (clés propres) et met à jour le
 * cache RAM. Aucun redémarrage n'est nécessaire : ils sont lus à chaque mesure
 * et à chaque affichage.
 */
bool settingsSaveSilo(const SiloConfig& silo);

/**
 * @brief Calibration courante des pieds (chargée au boot, cache RAM).
 * Valeurs par défaut = constantes compilées tant qu'aucune calibration n'a été
 * enregistrée via le portail.
 */
const Calibration& settingsGetCal();

/**
 * @brief Enregistre la calibration en NVS (clés propres, ne touche ni au WiFi
 * ni au MQTT) et met à jour le cache RAM.
 * @return true si l'écriture a réussi.
 */
bool settingsSaveCal(const Calibration& cal);
