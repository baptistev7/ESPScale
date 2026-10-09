#include <Arduino.h>

#include "app.hpp"
#include "state.hpp"
#include "time.hpp"
#include "power.hpp"
#include "settings.hpp"

// =============================================================================
// SETUP
// =============================================================================

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== ESP Scale Wakeup ===");

    // Initialise l'état persistant (reset si 1er boot / layout changé).
    stateInit();

    // Charge la configuration (NVS) AVANT tout usage réseau : WiFi et MQTT
    // lisent settingsGet().
    settingsLoad();

    // Configure le fuseau horaire AVANT tout affichage : sans ça, l'heure RTC
    // s'affiche en UTC quand le WiFi n'est pas activé ce cycle.
    timeConfigureTimezone();

    // PAS de displayInit() ici : l'écran garde sa dernière image hors tension.
    // displayShowMain() l'initialise avec reset à chaque cycle (une seule fois).

    // Route selon la cause de réveil (timer / bouton / redock / cold boot).
    appRun();
}

// =============================================================================
// LOOP (ne devrait jamais être atteint : appRun() finit toujours en deep sleep)
// =============================================================================

void loop() {
    Serial.println("ERROR: Loop should never be reached!");
    delay(1000);

    // Si on arrive ici, le deep sleep a échoué : on réessaie.
    ESP.restart();
}
