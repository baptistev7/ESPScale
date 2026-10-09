#include "scheduler.hpp"
#include "settings.hpp"   // aspiration enregistrée
#include "state.hpp"
#include "time.hpp"

// =============================================================================
// ORDONNANCEUR DE REVEIL
// =============================================================================

uint16_t schedulerWakeSlotFrom(uint16_t aspiration_min, uint8_t i) {
    // On part de l'aspiration ENREGISTRÉE. settingsNormalize() la borne déjà à
    // 0..1439, mais on re-bornne ici : cet ordonnanceur ne doit pas dépendre
    // d'un détail de chargement, sinon un réveil à « 25h » remonterait à l'écran.
    int a = (int)aspiration_min;
    if (a >= 1440) a = 18 * 60 + 30;

    int m = a + ((i == 0) ? -(int)kWakeOffsetMin : (int)kWakeOffsetMin);
    // modulo 1440 « positif » : -15 min → 1425, 1455 → 15
    m %= 1440;
    if (m < 0) m += 1440;
    return (uint16_t)m;
}

uint16_t schedulerWakeSlotMin(uint8_t i) {
    return schedulerWakeSlotFrom(settingsGet().aspiration_min, i);
}

uint32_t schedulerSecondsUntilNextWake() {
    // Si l'heure RTC est invalide (1er boot sans NTP), on ne peut pas
    // s'aligner : on dort une période fixe (12 h) puis on resynchronisera.
    if (!timeIsValid()) {
        return 12UL * 3600UL;
    }

    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    const int current_minutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;

    // Cherche le prochain créneau STRICTEMENT dans le futur.
    // Compare en minutes-depuis-minuit : le créneau suivant est aujourd'hui
    // (si > now) ou demain (si <= now, on ajoute 24 h).
    int delta_minutes = 24 * 60; // pire cas : demain même heure
    for (uint8_t i = 0; i < 2; i++) {
        int d = (int)schedulerWakeSlotMin(i) - current_minutes;
        if (d <= 0) d += 24 * 60; // créneau déjà passé → demain
        if (d < delta_minutes) delta_minutes = d;
    }

    const int delta_sec = delta_minutes * 60 - timeinfo.tm_sec;
    // delta_sec est >= 0 par construction (delta_minutes >= 1 dès qu'on est
    // pile sur un créneau, car d <= 0 → on ajoute 24 h).
    return static_cast<uint32_t>(delta_sec) + kRtcDriftCompensationSec;
}

// Secondes jusqu'au prochain 00:02 (horloge valide).
static uint32_t secondsUntilDayStart() {
    time_t now;
    time(&now);
    struct tm t;
    localtime_r(&now, &t);
    int d = (int)kNomadeDayStartMin - (t.tm_hour * 60 + t.tm_min);
    if (d <= 0) d += 24 * 60;                      // 00:02 passé → demain
    return (uint32_t)(d * 60 - t.tm_sec) + kRtcDriftCompensationSec;
}

// Veille nomade, hors charge : jusqu'au prochain 00:02, ou plus tôt si le
// quart de batterie estimé tombe avant (voir scheduler.hpp).
static uint32_t nomadeIdleSleepSeconds() {
    if (!timeIsValid()) return kNomadeNoClockSec;
    const uint32_t until = secondsUntilDayStart();
    return until < kNomadeQuarterSec ? until : kNomadeQuarterSec;
}

uint32_t schedulerSleepSeconds(ScaleMode mode) {
    switch (mode) {
        case ScaleMode::DEDOCKED:
            // L'état de charge AFFICHÉ (l'éclair), pas une relecture : c'est
            // aussi lui que l'ULP surveille : une relecture au dernier moment
            // pourrait contredire l'écran (l'ULP réveillerait alors la carte).
            return g_state.disp_charging ? kNomadeChargePollSec
                                         : nomadeIdleSleepSeconds();
        case ScaleMode::DOCKED: {
            // Réveil de 00:02 en dock : met la date de l'en-tête à jour (partial,
            // sans mesure — voir appDockedDateWake).
            const uint32_t slot = schedulerSecondsUntilNextWake();
            if (!timeIsValid()) return slot;
            const uint32_t midnight = secondsUntilDayStart();
            return midnight < slot ? midnight : slot;
        }
        case ScaleMode::DEGRADED:
        default:
            return schedulerSecondsUntilNextWake();
    }
}
