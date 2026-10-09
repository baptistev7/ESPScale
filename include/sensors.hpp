#pragma once

#include "config.hpp"

#include <Arduino.h>
#include <HX711.h>

// =============================================================================
// GESTION DES CAPTEURS HX711
// =============================================================================

/**
 * @brief Initialise tous les capteurs
 * Configure les broches, applique la calibration et attend que les capteurs
 * soient prêts (deadline globale de SENSOR_READY_TIMEOUT_MS)
 */
void sensorsInit();

/**
 * @brief Lit les 4 capteurs et retourne le poids total
 * @param attempts Nombre de tentatives par capteur
 * @return Poids total en kg (4/4 capteurs valides requis), ou -1 si erreur
 *         (dedock = 0 capteur, erreur dégradée = 1-3 capteurs)
 */
float sensorsReadTotalWeight(uint8_t attempts = SENSOR_READ_ATTEMPTS);

/**
 * @brief Nombre de capteurs valides lors du dernier sensorsReadTotalWeight()
 * @return 0-4 (0 = dedock / aucun capteur trouvable)
 */
uint8_t sensorsGetValidCount();

/**
 * @brief Coupe l'alimentation des capteurs (HX711 en power-down)
 * À appeler avant le deep sleep ; l'état est maintenu par prepareDeepSleep()
 */
void sensorsPowerOff();

/**
 * @brief Sort les capteurs du power-down
 */
void sensorsPowerOn();

/**
 * @brief Lecture d'UN pied : valeur BRUTE et poids étalonnés, en une seule
 * mesure (les deux valeurs sont donc cohérentes). Sert à la calibration depuis
 * le portail : le raw permet de tarer (Z) et de calculer le facteur d'échelle.
 * Pas de filtre de plausibilité : on veut la valeur telle quelle.
 * @param foot_index Index du pied (0-3)
 * @param raw_out Reçoit la valeur brute moyenne du HX711
 * @param kg_out  Reçoit le poids en kg (selon la calibration APPLIQUÉE)
 * @param samples Nombre de lectures à moyenner
 * @return false si le pied n'est pas disponible (jamais prêt à l'init, ou
 *         aucune lecture valide).
 */
bool sensorsReadFoot(uint8_t foot_index, float& raw_out, float& kg_out,
                     uint8_t samples = 3);

/**
 * @brief Réapplique la calibration courante (NVS) aux objets HX711.
 * Appelée à l'init, et à chaud par le portail après un enregistrement — pour
 * que l'affichage se mette à jour sans redémarrer.
 */
void sensorsApplyCalibration();
