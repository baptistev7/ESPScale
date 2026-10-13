#pragma once

// =============================================================================
// PORTAIL DE CONFIGURATION WEB (WiFi + MQTT + calibration des pieds)
// =============================================================================
// Lancé en maintenant le bouton GPIO 39 au démarrage (reset / power-on).
// Monte un AP + un portail captif pour saisir SSID/mot de passe WiFi et les
// paramètres MQTT, ainsi que les facteurs d'étalonnage Z/C des 4 pieds
// (page /cal), puis enregistre en NVS.
//
// Le portail garde l'ESP éveillé en AP (coûteux) : il se termine par un timeout
// d'inactivité.

/**
 * @brief Lance le portail de configuration. Enregistrement, appui ou timeout →
 * redémarrage ; SEULE exception : sans configuration enregistrée, la fonction
 * rend la main (l'appelant met la carte en veille).
 */
void webConfigRun();
