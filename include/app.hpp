#pragma once

// =============================================================================
// LOGIQUE APPLICATIVE (cycle de mesure, dispatch par cause de réveil)
// =============================================================================

/**
 * @brief Point d'entrée unique après le boot/réveil.
 * Route selon la cause de réveil :
 *  - TIMER    → cycle de mesure programmé (créneau autour de l'aspiration)
 *  - GPIO    → action selon l'état courant (remplissage, redock, ...)
 *  - COLD_BOOT→ premier boot / reset : cycle normal
 * Ne retourne jamais (finit toujours par un deep sleep).
 */
void appRun();
