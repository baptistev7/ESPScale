#include "app.hpp"

#include <Arduino.h>
#include <math.h>

#include "button.hpp"
#include "power.hpp"
#include "state.hpp"
#include "scheduler.hpp"
#include "sensors.hpp"
#include "mqtt.hpp"
#include "wifi.hpp"
#include "display.hpp"
#include "time.hpp"
#include "pins.hpp"
#include "webconfig.hpp"
#include "settings.hpp"


// =============================================================================
// HELPERS INTERNES
// =============================================================================

static void goToSleepFor(uint32_t sleep_sec);

// Instant du prochain réveil de MESURE (créneau d'aspiration), 0 = inconnu. Un
// réveil timer qui tombe avant (00:02) ne fait que la date de l'en-tête.
static RTC_DATA_ATTR time_t g_next_measure_at;


// Prépare et entre en deep sleep avec la durée adaptée au mode courant.
static void goToSleep() {
    const ScaleMode mode = stateGetMode();
    g_next_measure_at = (mode == ScaleMode::DOCKED && timeIsValid())
                            ? time(nullptr) + schedulerSecondsUntilNextWake() : 0;
    goToSleepFor(schedulerSleepSeconds(mode));
}

// Même chose, avec une durée imposée.
static void goToSleepFor(uint32_t sleep_sec) {
    sensorsPowerOff();

    // Fin de toute session interactive : on n'a plus besoin de capturer les
    // appuis, et on referme une éventuelle rafale d'affichage (powerOff avant
    // l'hibernation).
    buttonEnable(false);
    displaySetKeepPowered(false);

    // Point unique de mise en veille profonde de la dalle : c'est ce qui
    // préserve la RAM de trame entre deux réveils (et donc la possibilité de
    // faire des partial refresh au réveil suivant).
    displayPanelDeepSleep();

    ScaleMode mode = stateGetMode();
    Serial.printf("Next wakeup in: %u seconds\n", sleep_sec);

    // Arme les wake GPIO adaptés à l'état :
    //  - bouton (ext1) : mesure à la demande, lancement/continuation d'un refill,
    //    ou re-test manuel en dedock.
    //  - ligne DOUT (ext0) : réveil immédiat au dedock / redock (si câblée).
    switch (mode) {
        case ScaleMode::DOCKED:
            armRefillWakeup();
#if USE_DOCK_WAKE_DOUT
            armDedockWakeup();   // ext0 LOW : réveil immédiat au dédock
#endif
            break;
        case ScaleMode::DEGRADED:
            // 1-3/4 : un pied peut être HS et sa ligne DOUT bloquée basse →
            // ne PAS armer le réveil dedock (boucle de réveil garantie). Le
            // dédock sera vu au prochain cycle.
            armRefillWakeup();
            break;
        case ScaleMode::REFILL_POURING:
        case ScaleMode::REFILL_PRICING:
            // Ne devrait pas arriver : le parcours se fait carte éveillée et
            // toutes ses sorties repassent en DOCKED. Par sécurité, bouton +
            // timer (un réveil trouvant ce mode est traité en abandon).
            armRefillWakeup();
            break;
        case ScaleMode::DEDOCKED:
            armRefillWakeup();   // ext1 : re-test manuel
#if USE_CHRG_ULP
            // ULP : redock (ligne DOUT) + début/fin de charge (CHRG). Remplace
            // ext0, que l'IDF refuse de combiner avec l'ULP. En veille nomade
            // SEULEMENT : partout ailleurs l'ULP est arrêté, zéro coût.
            armNomadeUlpWakeup(g_state.disp_charging);
#elif USE_DOCK_WAKE_DOUT
            armRedockWakeup();   // ext0 HIGH : réveil immédiat au redock
#endif
            break;
    }

    // Toujours un timer de secours : une carte qui ne dort que sur le bouton
    // peut rester muette indéfiniment.
    prepareDeepSleep(sleep_sec);
    enterDeepSleep();
}

// (Déclarations anticipées : enterErrorState ouvre la session réseau — donc la
// synchro NTP — AVANT de dessiner l'écran dégradé qui porte la date de l'en-tête,
// et veille nomade n'ouvre qu'une session si l'horloge est périmée.)
static bool publishBegin();
static bool appSyncClockIfStale();

// Veille nomade : la date de l'en-tête affiché n'est plus celle du jour (l'écran
// a été dessiné avant minuit). Horloge invalide : on ne peut pas le savoir, donc
// non — c'est le poll léger qui retente la synchro et redessine s'il l'obtient.
static bool nomadeDateStale() {
    if (g_state.disp_drawn_at == 0) return true;
    if (!timeIsValid()) return false;
    const time_t now = time(nullptr);
    struct tm a, b;
    localtime_r(&g_state.disp_drawn_at, &a);
    localtime_r(&now, &b);
    return a.tm_yday != b.tm_yday || a.tm_year != b.tm_year;
}

// Bascule en état erreur/dedock et publie l'offline UNIQUEMENT à la transition.
// On se fie au mode d'erreur DÉJÀ ANNONCÉ (RTC) et non au mode courant :
// appRunScheduledCycle() remet le mode à DOCKED en début de cycle pour
// re-tester, donc un test sur le mode courant republierait l'offline à chaque
// réveil. En état stationnaire dedock, on ne fait que surveiller la charge
// (zone d'en-tête de l'écran nomade).
// 0/4 = le boîtier n'est plus sur sa base → écran « MODE NOMADE ». Tant que la
// résistance de tirage de la ligne DOUT n'est pas posée/testée, on ne peut pas
// distinguer un vrai dédock d'une panne capteur : « 0 capteurs » est donc lu
// comme « hors base » (TODO(dedock) — cf. docs/SPEC-ecrans.md).
static void enterErrorState(ScaleMode mode, uint8_t valid_count) {
    if (g_state.announced_error == mode) {
        // Erreur déjà annoncée : en dedock, on rafraîchit seulement la zone
        // d'en-tête (éclair + batterie) si le % ou l'état de charge a changé
        // (partial zoné, ~856 ms, sans flash) — inutile de redessiner le silo
        // endormi, le dernier poids et les textes.
        stateSetMode(mode); // rétablit le mode réel (reset DOCKED en amont)
        if (mode == ScaleMode::DEDOCKED) {
            // Cycle complet de la veille nomade (00:02 en général) : recale
            // l'horloge si elle a plus de 24 h (dérive RTC), puis redessine tout
            // si la date a changé, sinon la seule icône batterie.
            appSyncClockIfStale();
            uint8_t batt = getBatteryPercentage();
            bool chg = isCharging();
            if (g_state.disp_screen != DisplayScreen::DEDOCK || nomadeDateStale()) {
                Serial.println("Dedock: new day, nomade screen redrawn");
                displayShowNomade(batt, chg);
            } else if (displayBatteryIconChanged(batt, chg)) {
                Serial.printf("Dedock: battery update %u%% (chg=%d)\n", batt, chg);
                displayShowNomadeBattery(batt, chg);
            }
        }
        goToSleep();
        return;
    }

    // Transition : affiche puis publie l'état offline.
    stateSetMode(mode);

    // L'écran dégradé est la page PRINCIPALE, donc AVEC horloge : on ouvre le
    // réseau avant de le dessiner pour afficher l'heure corrigée par le NTP.
    // Le dedock a son écran dédié sans horloge — et ne doit pas réveiller le
    // WiFi pour rien. (Une seule tentative de connexion dans les deux cas.)
    bool wifi_up = true;
    if (mode != ScaleMode::DEDOCKED) {
        wifi_up = publishBegin();
    } else {
        // Veille nomade : on n'ouvre PAS de session réseau pour publier… mais
        // l'en-tête de l'écran nomade porte la date, donc il faut une horloge
        // valide. Une seule session, et seulement si l'heure est invalide
        // (coupure d'alimentation) ou vieille de plus de 24 h.
        appSyncClockIfStale();
    }

    if (mode == ScaleMode::DEDOCKED) {
        displayShowNomade(getBatteryPercentage(), isCharging());
    } else {
        // Dégradé (1-3 capteurs) : le boîtier est en place, on garde la page
        // principale avec l'indicateur d'erreur.
        displayShowMain(-1.0f, getBatteryPercentage(), isCharging());
    }

    if (wifi_up && wifiConnect()) {
        if (mqttConnect()) {
            mqttPublishError(true);
            mqttPublishQuality(valid_count,
                               mode == ScaleMode::DEDOCKED ? "dedock" : "degraded");
            mqttDisconnect();
        }
        wifiDisconnect();
    }
    g_state.announced_error = mode;
    goToSleep();
}

// Horloge de l'en-tête (la date est dessinée sur TOUS les écrans).
//
// Le RTC interne dérive de plusieurs % (RTC_SLOW_CLK = oscillateur RC interne,
// pas de quartz 32 kHz, cf. time.cpp) et il est PERDU à chaque coupure
// d'alimentation : sans synchro NTP l'en-tête affiche « --/--/-- » (arrêt à
// froid) ou une date fausse de plusieurs dizaines de minutes par jour.
//
// Deux chemins la récupèrent :
//   - toute session réseau déjà ouverte, via publishBegin() ci-dessous ;
//   - appSyncClockIfStale() pour ce qui n'ouvre JAMAIS le réseau : veille nomade
//     et cycle sans publication.
//
// Le « dernier sync » est en RAM RTC : il survit au deep sleep, pas à une
// coupure d'alimentation.
static RTC_DATA_ATTR uint32_t g_ntp_last_sync;   // 0 = jamais synchronisé

// Ouvre UNE session réseau pour resynchroniser l'horloge — mais seulement si
// elle est INVALIDE ou vieille de plus de 24 h. Aucun réseau dans le cas courant :
// c'est ce qui permet aux écrans de veille de ne jamais payer de session.
static bool appSyncClockIfStale() {
    const time_t now = time(nullptr);
    const bool invalid = !timeIsValid();
    const bool aged = (g_ntp_last_sync == 0) ||
                      (now > (time_t)g_ntp_last_sync + 24 * 3600);
    if (!invalid && !aged) return false;          // rien à faire, pas de session
    Serial.println("Clock: syncing (invalid or older than 24h)");
    if (!wifiConnect()) return false;
    const bool ok = timeInit();                   // attend la réponse du SNTP
    wifiDisconnect();
    if (ok) g_ntp_last_sync = (uint32_t)time(nullptr);
    return ok;
}

// Ouvre la session réseau : WiFi puis synchro NTP. Volontairement séparé de
// publishSend() pour que le cycle planifié puisse afficher l'écran principal
// ENTRE les deux — l'en-tête doit porter la date corrigée par le NTP, pas la date
// RTC dérivée depuis le dernier réveil.
static bool publishBegin() {
    if (!wifiConnect()) {
        Serial.println("WiFi connection failed!");
        return false;
    }

    // Synchronise l'heure via NTP (le RTC dérive entre deux réveils ; timeInit
    // garde l'heure RTC locale si le NTP ne répond pas).
    if (timeInit()) g_ntp_last_sync = (uint32_t)time(nullptr);
    return true;
}

// Publie poids + batterie + erreur + qualité, puis referme le WiFi. Suppose la
// session réseau déjà ouverte par publishBegin().
static bool publishSend(float weight, uint8_t battery_percent) {
    bool sent = false;
    if (mqttConnect()) {
        bool w = mqttPublishWeight(weight);
        bool b = mqttPublishBattery(battery_percent);
        mqttPublishError(false);                    // capteurs OK
        mqttPublishQuality(sensorsGetValidCount(), "ok");
        sent = w && b;
        mqttDisconnect();
    }
    wifiDisconnect();
    return sent;
}

// Session réseau complète (WiFi + NTP + publication), pour les appelants qui
// n'ont pas d'affichage d'horloge à intercaler (fin de remplissage, annulation).
static bool publishData(float weight, uint8_t battery_percent) {
    return publishBegin() && publishSend(weight, battery_percent);
}

// Cliquet de publication "gel au mini" : le palier publié ne remonte JAMAIS sur
// une hausse < 1 sac. Une hausse < kRefillWildMinDeltaKg est présumée bruit /
// dérive (thermique, fluage) → le palier reste gelé au minimum et RIEN n'est
// publié tant qu'on ne repasse pas sous ce minimum. Une dérive réversible ne
// s'accumule donc pas en fausse conso.
// Seules deux choses déplacent le palier :
//   - une baisse réelle ≥ MQTT_SEND_DELTA_KG (consommation) → palier descend ;
//   - une hausse ≥ kRefillWildMinDeltaKg (~1 sac, refill sauvage) → palier monte.
static constexpr float kRefillWildMinDeltaKg = 14.0f; // ~1 sac de granulés

// Décision de publication du palier.
//  send  : faut-il (re)publier ?
//  value : poids à publier (le palier courant, ou la nouvelle mesure)
//  moved : le palier doit-il être remplacé par `value` ?
// Le heartbeat / la batterie / le retry republient le palier INCHANGÉ : le stock
// HA ne bouge pas (rafraîchit seulement le retain après un redémarrage broker).
struct PublishDecision {
    bool  send;
    float value;
    bool  moved;
};

static PublishDecision decidePublish(float weight, uint8_t battery_percent) {
    if (!g_state.has_last_weight) return {true, weight, true};            // 1re mesure
    const float delta = weight - g_state.last_weight_kg;
    if (delta <= -MQTT_SEND_DELTA_KG) return {true, weight, true};        // conso
    if (delta >= kRefillWildMinDeltaKg) return {true, weight, true};      // refill sauvage
    if (!g_state.last_send_ok) return {true, g_state.last_weight_kg, false}; // retry
    if (abs((int)battery_percent - (int)g_state.last_battery_pct) >= MQTT_BATTERY_DELTA_PCT)
        return {true, g_state.last_weight_kg, false};                     // batterie
    if (g_state.skip_count >= MQTT_HEARTBEAT_CYCLES)
        return {true, g_state.last_weight_kg, false};                     // heartbeat
    return {false, 0.0f, false}; // hausse < 1 sac : palier gelé, aucune publication
}

// Met à jour l'état RTC après une tentative de publication. Le palier ne bouge
// que si `moved` (nouvelle mesure basse ou refill sauvage) — jamais sur heartbeat.
static void updatePublishState(const PublishDecision& pub, bool attempted,
                               bool sent, uint8_t battery_percent) {
    if (!attempted) {
        g_state.skip_count++;
        return;
    }
    g_state.last_send_ok = sent;
    if (sent) {
        if (pub.moved) g_state.last_weight_kg = pub.value;
        g_state.last_battery_pct = battery_percent;
        g_state.has_last_weight = true;
        g_state.skip_count = 0;
    }
}

// Après un refill DÉCLARÉ, on connaît le nouveau poids du silo (mesuré) : on
// relève le palier RTC immédiatement, que l'envoi ait réussi ou non —
//  - publié : HA voit la hausse sur scale/value, le stock kg est à jour ;
//  - échec   : last_send_ok=false → le cycle suivant republie le palier (retry).
// Sans ça, le cycle suivant re-détecterait la hausse ≥ 1 sac comme un « refill
// sauvage » et émettrait un SECOND event (double comptage € côté HA).
static void commitRefillPalier(float weight_after, uint8_t battery_percent, bool sent) {
    g_state.last_weight_kg = weight_after;
    g_state.has_last_weight = true;
    g_state.last_battery_pct = battery_percent;
    g_state.last_send_ok = sent;
    g_state.skip_count = 0;
}

// =============================================================================
// CYCLE DE MESURE NORMAL (réveil timer, état DOCKED/DEGRADED)
// =============================================================================

// Mémorise la dernière mesure VALIDE (4/4) : poids + horodatage. Affichée par
// l'écran « MODE NOMADE » quand le boîtier est retiré (aucune mesure possible).
// L'horodatage n'est posé que si l'heure RTC est valide (sinon il resterait
// faux) : l'écran affiche alors « MESURE SANS HEURE ».
static void recordMeasurement(float weight_kg) {
    if (weight_kg < 0.0f) return;
    g_state.last_measure_kg = weight_kg;
    g_state.has_last_measure = true;
    if (timeIsValid()) g_state.last_measure_at = time(nullptr);
}

// (Déclarations anticipées : appRunScheduledCycle reprend un refill interrompu
// par un dedock → refillPourAndSave / refillConfirmAndSave ; le menu appelle
// refillStart, définie plus bas.)
static void refillAbandon(const char* why);
static void refillPourAndSave();     // reprise « mesure avant » → tout le parcours
static void refillConfirmAndSave();  // reprise « mesure après » → E14 → E15
static void refillStepBags();                // E12 (aussi : correction)
static void refillStepPrice();               // E13 (retour à la saisie)
static void refillMeasureAfterAndConfirm();  // mesure « après » → E14

// VEILLE NOMADE — POLL LÉGER : la CHARGE seule (réveils de charge toutes les
// kNomadeChargePollSec, réveil avant minuit, appui court).
//
// On n'allume PAS les 4 HX711 (2,1 s de `wait_ready`), on ne publie rien, et
// l'e-paper n'est rafraîchi que si l'icône a changé (segments ou éclair :
// partial de la zone d'en-tête).
//
// Le cycle complet — capteurs, état réel, date — revient quand la date a changé
// (réveil de 00:02).
static void appRunNomadeLightPoll() {
    const uint8_t batt = getBatteryPercentage();
    const bool chg = isCharging();

    // L'horloge a pu être perdue depuis le dernier réveil (coupure
    // d'alimentation) ou la synchro avoir échoué faute de réseau : on réessaie,
    // et si elle aboutit on redessine l'écran, sinon la date resterait « --/--/-- »
    // pendant toute la veille. Une fois la date valide, plus jamais de session.
    if (!timeIsValid() && appSyncClockIfStale()) {
        Serial.println("Nomade poll: horloge recuperee, ecran redessine");
        displayShowNomade(batt, chg);       // full : la date est au changement d'écran
        goToSleep();
        return;
    }

    if (g_state.disp_screen != DisplayScreen::DEDOCK) {
        displayShowNomade(batt, chg);       // autre écran affiché : on le remplace
    } else if (displayBatteryIconChanged(batt, chg)) {
        Serial.printf("Nomade poll: battery %u%% (charging=%d)\n", batt, chg);
        displayShowNomadeBattery(batt, chg);
    }
    goToSleep();   // en charge : 5 min ; sinon 00:02 — ne revient pas
}

// Réveil timer de 00:02 en dock, loin du prochain créneau de mesure : partial de
// la date seule, puis on se rendort (le créneau de mesure est conservé). false =
// c'est un vrai créneau (ou l'écran ne s'y prête pas) : cycle complet.
static bool appDockedDateWake() {
    if (stateGetMode() != ScaleMode::DOCKED || g_next_measure_at == 0 ||
        !timeIsValid() || time(nullptr) + 60 >= g_next_measure_at) {
        return false;
    }
    if (!displayRefreshMainDate()) return false;
    Serial.println("Docked date wake: header date refreshed");
    goToSleep();
    return true;
}

static void appRunScheduledCycle(bool force_full = false) {
    // VEILLE NOMADE : poll léger (charge seule) tant que la date affichée est
    // celle du jour ; le réveil de 00:02 (date changée) fait le cycle complet.
    ScaleMode cur = stateGetMode();
    if (cur == ScaleMode::DEDOCKED && !force_full && !nomadeDateStale() &&
        // Sous le seuil d'urgence, c'est le cycle complet qui doit jouer
        // (garde batterie : 24 h / 1 semaine).
        readBatteryVoltage() >= BATTERY_EMERGENCY_THRESHOLD_V) {
        appRunNomadeLightPoll();
        return;
    }

    // Parcours de remplissage interrompu : le parcours se fait carte ÉVEILLÉE et
    // toutes ses sorties repassent en DOCKED, donc retrouver ce mode ici veut
    // dire qu'un reset (watchdog, chute de tension) l'a coupé. La session est
    // perdue : abandon, comme un délai dépassé — cycle de mesure normal, refill
    // sauvage s'il y a eu versement. Jamais d'envoi avec un prix non confirmé.
    if (cur == ScaleMode::REFILL_POURING || cur == ScaleMode::REFILL_PRICING) {
        refillAbandon("interrupted by a reset");
        return;
    }

    Serial.println("=== Scheduled cycle ===");
    stateSetMode(ScaleMode::DOCKED); // on re-teste : si les capteurs sont KO on repassera en DEGRADED/DEDOCKED

    // 0. Protection batterie basse tension (le T5 n'a pas de déconnexion HW).
    //    Mesurée au réveil (à froid, donc conservateur).
    float vbat = readBatteryVoltage();
    if (vbat < BATTERY_SHUTDOWN_THRESHOLD_V) {
        if (vbat < BATTERY_EMERGENCY_THRESHOLD_V) {
            g_state.low_batt_count++;
            Serial.printf("LOW BATTERY (%.2fV) emergency, sleeping 24h (count %u)\n",
                          vbat, g_state.low_batt_count);
            if (g_state.low_batt_count >= BATTERY_EMERGENCY_CYCLES) {
                Serial.println("Battery critically low, long sleep until charge");
                sensorsPowerOff();
                prepareDeepSleep(kCriticalSleepSec); // 1 semaine
                enterDeepSleep();
            }
            sensorsPowerOff();
            prepareDeepSleep(kEmergencySleepSec);
            enterDeepSleep();
        }
        Serial.printf("LOW BATTERY (%.2fV), skipping cycle\n", vbat);
        sensorsPowerOff();
        prepareDeepSleep(schedulerSecondsUntilNextWake());
        enterDeepSleep();
    }
    g_state.low_batt_count = 0; // batterie OK

    // 1. Mesure le poids (strict 4/4).
    sensorsInit();
    float weight = sensorsReadTotalWeight();

    if (weight < 0) {
        // Pas de mesure fiable : dedock (0 capteur) ou dégradé (1-3).
        uint8_t valid_count = sensorsGetValidCount();
        ScaleMode err_mode = (valid_count == 0) ? ScaleMode::DEDOCKED : ScaleMode::DEGRADED;
        Serial.printf("No reliable weight (%u/4 valid), mode=%s\n",
                      valid_count, stateModeName(err_mode));

        // Si un refill était en attente de redock, on vérifie son timeout :
        // au-delà de 1 h sans silo → abandon propre (flag effacé).
        if (g_state.refill_pending != RefillPhase::NONE && err_mode == ScaleMode::DEDOCKED) {
            time_t now = time(nullptr);
            if (g_state.refill_started_at != 0 &&
                (now - g_state.refill_started_at) > (time_t)kRefillPendingSec) {
                Serial.println("[pending] refill abandoned (>1h without silo)");
                g_state.refill_pending = RefillPhase::NONE;
            }
        }
        enterErrorState(err_mode, valid_count);
        return; // ne devrait pas être atteint (goToSleep ne revient pas)
    }

    // Mesure 4/4 valide : on mémorise le dernier poids connu (écran nomade).
    recordMeasurement(weight);

    // REPRISE d'un refill interrompu par un dedock : le silo est revenu (mesure
    // valide) et un refill est en attente → on reprend la FSM au bon endroit.
    if (g_state.refill_pending != RefillPhase::NONE) {
        Serial.printf("[pending] silo back, resuming refill (phase=%d)\n",
                      (int)g_state.refill_pending);
        RefillPhase phase = g_state.refill_pending;
        g_state.refill_pending = RefillPhase::NONE;

        if (phase == RefillPhase::BEFORE_MEASURE) {
            // Rien n'a encore été saisi : on reprend à la mesure « avant ».
            refillPourAndSave();   // re-mesure puis E11 → E12 → E13 → E14
            return;                // ne revient pas (dort ou continue)
        }
        // AFTER_MEASURE : les sacs sont comptés et le prix est saisi, on refait
        // la mesure « après » puis on réaffiche E14 (rien n'a été publié).
        refillMeasureAfterAndConfirm();
        refillConfirmAndSave();   // → E14 → (maintien) → E15
        return;                   // ne revient pas
    }

    // Mesure 4/4 valide : l'état nominal est rétabli. Une erreur ANNONCÉE
    // (dédock, capteurs KO) a laissé `error/on-off = on` en retain côté HA : le
    // retour est un changement d'état, il se publie même si le poids n'a pas
    // bougé. Effacé APRÈS la reprise d'un refill ci-dessus, pour que ce cas
    // reste vrai si le refill repart en abandon.
    const bool back_from_error = (g_state.announced_error != ScaleMode::DOCKED);
    g_state.announced_error = ScaleMode::DOCKED;

    // 2. Mesure batterie + charge.
    uint8_t battery_percent = getBatteryPercentage();
    float battery_voltage = readBatteryVoltage();
    bool is_charging = isCharging();
    Serial.printf("Weight: %.2f kg | Battery: %.2fV (%d%%) | Charging: %d\n",
                  weight, battery_voltage, battery_percent, is_charging);

    // 3. Décide si on publie (cliquet "gel au mini" + heartbeat).
    PublishDecision pub = decidePublish(weight, battery_percent);
    if (!pub.send && back_from_error) {
        Serial.println("Back from error: publishing the docked state");
        pub = g_state.has_last_weight
                  ? PublishDecision{true, g_state.last_weight_kg, false}
                  : PublishDecision{true, weight, true};
    }
    bool sent = false;

    if (pub.send) {
        // Détecte un REFILL SAUVAGE : hausse ≥ ~1 sac SANS passer par la FSM
        // (ex: appoint pendant un trou de mesure / balance éteinte). On publie
        // l'événement pour que HA valorise le stock au prix moyen courant
        // (bag_count=0 → HA sait que le prix n'a pas été déclaré).
        const float delta = pub.value - g_state.last_weight_kg;
        bool wild_refill = g_state.has_last_weight && pub.moved &&
                           delta >= kRefillWildMinDeltaKg;
        if (wild_refill) {
            Serial.printf("Wild refill detected: +%.1f kg\n", delta);
        }
        // Session réseau AVANT l'affichage : la barre d'état doit porter l'heure
        // corrigée par le NTP (le RTC a dérivé depuis le dernier réveil). Le MQTT
        // reste APRÈS l'affichage : l'écran n'attend pas la publication.
        bool wifi_up = publishBegin();
        // État d'envoi affiché : le WiFi est déjà connu ici ; le résultat MQTT
        // ne l'est qu'après, d'où un second affichage (partial) s'il change.
        const uint8_t fault_before = wifi_up ? 0 : 1;
        displaySetSendFault(fault_before);
        displayShowMain(weight, battery_percent, is_charging);
        sent = wifi_up && publishSend(pub.value, battery_percent);
        const uint8_t fault_after = sent ? 0 : (wifi_up ? 2 : 1);
        if (fault_after != fault_before) {
            displaySetSendFault(fault_after);
            displayShowMain(weight, battery_percent, is_charging);
        }
        if (sent && wild_refill) {
            // Event complémentaire : HA ajoute la valeur au prix moyen du stock.
            if (wifiConnect() && mqttConnect()) {
                mqttPublishRefillEvent(delta, 0.0f, 0);
                mqttDisconnect();
                wifiDisconnect();
            }
        }
        if (sent) {
            Serial.println("Data sent successfully!");
        } else {
            Serial.println("Failed to send data!");
        }
    } else {
        // Palier gelé : aucune publication, donc aucune session réseau ouverte…
        // mais l'en-tête de `MAIN` porte la date. Resync uniquement si l'heure
        // est invalide ouâgée de plus de 24 h (dérive du RTC).
        appSyncClockIfStale();
        Serial.println("Rise below one bag: plateau frozen, no publish");
        displaySetSendFault(0);   // pas d'envoi en attente : le dernier a réussi
        displayShowMain(weight, battery_percent, is_charging);
    }
    updatePublishState(pub, pub.send, sent, battery_percent);

    // 4. Temps d'éveil + deep sleep.
    goToSleep();
}

// =============================================================================
// MODE REMPLISSAGE (bouton GPIO 39)
// =============================================================================

// Fenêtre d'inactivité AVANT de se rendormir pendant une session de
// remplissage : 10 min. Si la balance dormait entre deux gestes (timeout trop
// court), le clic suivant servirait à la réveiller et serait perdu (clic
// fantôme). 10 min = on ne dort que si l'utilisateur abandonne vraiment.
constexpr uint32_t kRefillSessionTimeoutMs = 600000UL; // 10 min

// Tenue nécessaire pour ENREGISTRER un remplissage (ECRAN14). Partagée avec la
// confirmation du portail (kHoldConfirmMs) : les deux écrans sont des « je
// valide en tenant », la durée doit donc être la même des deux côtés.
constexpr uint32_t kHoldConfirmMs = 2500UL; // 2,5 s

// Mesure fiable 4/4 → poids, sinon -1. Mémorise la mesure (écran nomade).
static float measureWeight() {
    sensorsInit();
    float w = sensorsReadTotalWeight();
    recordMeasurement(w);
    return w;
}

// ABANDON du parcours (délai dépassé, relâché sans geste, Δ négatif…) : le
// parcours ne publie RIEN lui-même — surtout pas un prix saisi mais non confirmé.
// On repasse en DOCKED et on enchaîne un cycle de mesure normal : s'il y a eu
// versement (hausse ≥ 1 sac), le cliquet le publie en « refill sauvage »
// (bag_count = 0, valorisé au prix moyen côté HA). L'écran repasse aussi sur
// l'écran principal au lieu de laisser l'écran du parcours affiché en veille.
static void refillAbandon(const char* why) {
    Serial.printf("Refill abandoned (%s) -> normal cycle (wild refill if poured)\n", why);
    g_state.refill_pending = RefillPhase::NONE;
    displaySetKeepPowered(false);
    stateSetMode(ScaleMode::DOCKED);
    appRunScheduledCycle(/*force_full=*/true);   // ne revient pas
}

// Attend le relâchement du bouton (anti-rebond après réveil).
static void waitButtonRelease(uint32_t timeout_ms) {
    uint32_t start = millis();
    while (readRefillButton() && (millis() - start) < timeout_ms) {
        delay(20);
    }
}

// Jette l'appui qui a amené dans la session (réveil, reprise) : attend son
// relâchement — la file ISR enregistre tout — puis remet la file à zéro.
// Les gestes faits ENSUITE (pendant le render, le WiFi…) sont conservés : c'est
// tout l'intérêt de ne vider la file qu'à ce moment précis.
static void absorbWakePress() {
    if (!readRefillButton()) return; // rien de maintenu : rien à jeter
    waitButtonRelease(3000);
    buttonReset();
}

// Ouvre une session interactive : capture des appuis par timer matériel (donc
// pendant les blocages : refresh, WiFi…) + absorption de l'appui de réveil.
// À n'appeler qu'UNE fois par session (buttonEnable(true) remet à zéro).
static void enterInteractiveSession() {
    buttonEnable(true);
    absorbWakePress();
}

// Détecte un appui long (≥ 1 s) après un réveil par le bouton.
// Retourne false pour un appui court (utilisé pour une mesure à la demande).
static bool waitLongPress(uint32_t timeout_ms) {
    uint32_t start = millis();
    // Attend l'appui
    while (!readRefillButton() && (millis() - start) < timeout_ms) {
        delay(20);
    }
    if (!readRefillButton()) return false; // timeout, pas d'appui

    // Mesure la durée de l'appui (relâchement < 1 s = court, maintien = long)
    uint32_t press_start = millis();
    while (readRefillButton() && (millis() - press_start) < 1000) {
        delay(20);
    }
    // Si le bouton est encore appuyé après 1 s → appui long validé.
    return readRefillButton();
}

// =============================================================================
// PARCOURS DE REMPLISSAGE (5 écrans)
// =============================================================================
//
//   menu OPTIONS ──▶ E11 poids versé ──▶ E12 nombre de sacs ──▶ E13 prix du sac
//                                                              │
//   E15 « ENREGISTRÉ » ◀── E14 « TERMINER ? » (maintien 2,5 s) ◀┘  (mesure « après »)
//
// L'ordre est celui du terrain : on verse d'abord (le poids se voit), on compte
// les sacs, et le PRIX se saisit APRÈS — c'est le seul moyen de ne pas avoir à
//Ended tenir le bouton pendant tout le versement. L'enregistrement est un
// MAINTIEN : une déclaration ne doit pas se perdre sur un appui appuyé.
//
// Chaque étape a sa fenêtre de 10 min ; au-delà, abandon propre (rien n'est
// publié, le cycle suivant rattrape le poids). Un dedock en cours de route met
// le refill en attente (RefillPhase) et le redock le reprend où il s'était
// arrêté.
//
// Déclarations anticipées (les reprises du cycle planifié et l'enchaînement
// interne s'appellent mutuellement).

// Publie l'état du parcours (`refill_state` retenu) : le vocabulaire suit les
// écrans — `pouring` pendant le versement et le comptage, `pricing` pendant la
// saisie, `saved` une fois enregistré. L'état PHYSIQUE (docked / dedocked) reste
// porté par `quality.mode` : pendant tout le parcours le silo est hors de sa
// base, il ne faut donc PAS annoncer « docked » ici.
static void publishRefillState(const char* state) {
    if (wifiConnect() && mqttConnect()) {
        mqttPublishRefillState(state);
        mqttDisconnect();
    }
    wifiDisconnect();
}

// Démarre un remplissage depuis le menu OPTIONS.
// (Déclaration anticipée : le menu appelle cette fonction, définie ici.)
static void refillStart() {
    Serial.println("=== Refill: START (from menu) ===");

    // Session interactive : file d'appuis sans perte + capture pendant les
    // refresh, et absorption de l'appui long qui a réveillé.
    enterInteractiveSession();

    // Garde batterie : ne pas démarrer un refill si la tension est trop basse
    // (un stop intempestif en plein versement perdrait la FSM / la mesure).
    float vbat = readBatteryVoltage();
    if (vbat < BATTERY_SHUTDOWN_THRESHOLD_V) {
        Serial.printf("Refill blocked: battery too low (%.2fV < %.2fV)\n",
                      vbat, BATTERY_SHUTDOWN_THRESHOLD_V);
        stateSetMode(ScaleMode::DOCKED);
        goToSleep(); // l'écran garde son image, on retentera après recharge
        return;
    }

    g_state.refill_started_at = time(nullptr); // timeout d'abandon (2 h)
    g_state.refill_delta_kg = 0.0f;
    g_state.refill_bag_count = 0;
    g_state.price_digits[0] = 4; // défaut 4,05 €
    g_state.price_digits[1] = 0;
    g_state.price_digits[2] = 5;
    g_state.price_idx = 0;

    refillPourAndSave(); // mesure « avant » puis tout le parcours
}

// Mesure « AVANT » puis enchaîne les 3 étapes. Point d'entrée du parcours et de
// la reprise après un dédock survenu avant cette mesure (RefillPhase::BEFORE_MEASURE).
static void refillPourAndSave() {
    Serial.println("=== Refill: measure BEFORE ===");

    float before = measureWeight();
    if (before < 0) {
        uint8_t vc = sensorsGetValidCount();
        if (vc == 0) {
            // CAS A — silo retiré avant la mesure « avant » : on ne perd pas le
            // refill (rien n'a encore été saisi). Reprise au redock.
            Serial.println("Dedock before measure-before: pending refill");
            g_state.refill_pending = RefillPhase::BEFORE_MEASURE;
            // Le timestamp de début sert aussi au timeout du pending.
            if (g_state.refill_started_at == 0) {
                g_state.refill_started_at = time(nullptr);
            }
            stateSetMode(ScaleMode::DEDOCKED);
            displayShowNomade(getBatteryPercentage(), isCharging());
            goToSleep(); // wake: timer (dedock long) / redock si contact
            return;
        }
        // 1-3 capteurs : erreur capteurs (pas un dedock) → on annule.
        Serial.println("Refill aborted: sensors degraded");
        enterErrorState(ScaleMode::DEGRADED, vc);
        return;
    }

    // Mesure « avant » OK → on efface le flag pending et on passe au versement.
    g_state.refill_pending = RefillPhase::NONE;
    g_state.refill_before_kg = before;
    g_state.refill_bag_count = 0;
    stateSetMode(ScaleMode::REFILL_POURING);

    // Publié AVANT le render : ce bloc est BLOQUANT (WiFi + MQTT). Placé après
    // l'affichage, la fenêtre morte tomberait pile quand l'utilisateur voit
    // l'écran et commence à verser.
    publishRefillState("pouring");

    // --- ÉTAPE 1/3 : le poids se lit pendant le versement -------------------
    // `● COURT` = je continue vers le comptage des sacs. Le pied n'annonce que
    // ce geste : l'appui long est INERTE ici (rien à faire d'autre), on ne
    // dessine donc pas de `— LONG` qui serait un mensonge.
    //
    // Le poids n'a aucun geste pour le rafraîchir : on SONDE le capteur toutes
    // les kLiveProbeMs, et on ne rafraîchit que si ça bouge vraiment (≥ 0,5 kg,
    // soit le bruit de fond). Un full par lecture (~1,3 s) serait inutilisable,
    // et aucun refresh ne serait pire : l'écran « en direct » qui ne bouge pas
    // est un écran mort.
    const uint32_t kLiveWindowMs   = kRefillSessionTimeoutMs;
    const uint32_t kLiveProbeMs    = 3000;
    const float    kLiveEpsKg     = 0.5f;

    Serial.println("=== Refill: step 1/3 pouring ===");
    float last_shown = 0.0f;
    bool go_bags = false;
    {
        uint8_t batt = getBatteryPercentage();
        bool chg = isCharging();
        const float cap = settingsGetSilo().capacity_kg;
        uint8_t lvl = (uint8_t)((before * 100.0f) / (cap > 0.0f ? cap : TANK_FULL_KG));
        displayShowRefillLive(batt, chg, /*added_kg=*/0.0f, before, lvl);

        uint32_t start = millis(), last_probe = millis();
        while ((millis() - start) < kLiveWindowMs) {
            if (buttonTakeShorts()) {
                Serial.println("Step 1: -> bags");
                go_bags = true;
                break;
            }
            buttonTakeLong(); // inerte : on le consomme pour qu'il ne traîne pas
            if ((millis() - last_probe) >= kLiveProbeMs) {
                last_probe = millis();
                float w = measureWeight();
                if (w >= 0.0f) {
                    float a = w - g_state.refill_before_kg;
                    if (a < 0.0f) a = 0.0f;
                    if (fabsf(a - last_shown) >= kLiveEpsKg) {
                        last_shown = a;
                        displayShowRefillLiveUpdate(a, w, (uint8_t)((w * 100.0f) /
                            (cap > 0.0f ? cap : TANK_FULL_KG)));
                    }
                }
            }
            delay(5);
        }
    }
    if (!go_bags) {
        // Timeout sans geste : ABANDON. Ne surtout pas enchaîner sur l'étape 2 :
        // on demanderait un compte de sacs pour un versement qui n'a pas eu lieu.
        refillAbandon("pouring timeout");
        return;
    }

    refillStepBags();
    refillStepPrice();   // E13 : manquait depuis la refonte (E12 → E14 direct,
                         // au prix par défaut)
    refillMeasureAfterAndConfirm();
    refillConfirmAndSave();
}

// ÉTAPE 2/3 — ECRAN12 : nombre de sacs. `● COURT` = +1 sac (partial zoné),
// `— LONG VALIDER` = le compte y est. Repart du compte en cours (0 au début
// du parcours et après « CORRIGER ? → NOMBRE DE SACS »). Ne revient pas sur
// timeout (abandon).
static void refillStepBags() {
    Serial.println("=== Refill: step 2/3 bags ===");
    {
        uint8_t batt = getBatteryPercentage();
        bool chg = isCharging();
        displayShowRefillBags(batt, chg, g_state.refill_bag_count);

        uint32_t start = millis(), last_gesture = millis();
        bool next = false;
        while ((millis() - start) < kRefillSessionTimeoutMs && !next) {
            if (buttonTakeLong()) {
                Serial.printf("Bags validated: %u\n", g_state.refill_bag_count);
                next = true;
                break;
            }
            uint8_t n = buttonTakeShorts();
            if (n) {
                displaySetKeepPowered(true); // rafale : _PowerOn payé une fois
                last_gesture = millis();
            }
            for (uint8_t i = 0; i < n; i++) {
                g_state.refill_bag_count++;
                Serial.printf("Bag +1: total %u\n", g_state.refill_bag_count);
                // Full tous les 10 sacs (anti-ghosting résiduel), partial entre-temps.
                if (g_state.refill_bag_count % 10 == 0) {
                    displayShowRefillBags(batt, chg, g_state.refill_bag_count);
                } else {
                    displayShowRefillBagsUpdate(g_state.refill_bag_count);
                }
            }
            if (!n && (millis() - last_gesture > 1500)) {
                displaySetKeepPowered(false); // plus de geste : on coupe la dalle
            }
            delay(5);
        }
        if (!next) {
            refillAbandon("bags timeout");
            return;
        }
    }
}

// ÉTAPE 3/3 — ECRAN13 : le prix du sac, digit par digit. Retourne quand le
// prix est VALIDÉ ; ne retourne pas si l'utilisateur abandonne (timeout → le
// parcours s'endort proprement).
static void refillStepPrice() {
    // `● COURT` = +1 sur le digit courant, `— LONG` = digit suivant puis
    // VALIDER sur le dernier. VALIDER enchaîne la mesure « après » puis E14.
    Serial.println("=== Refill: step 3/3 price ===");
    // L'état du parcours suit l'écran : on saisit le prix. Publié AVANT le
    // render (ce bloc est bloquant : WiFi + MQTT).
    publishRefillState("pricing");
    stateSetMode(ScaleMode::REFILL_PRICING);
    uint8_t idx = g_state.price_idx;
    uint8_t batt = getBatteryPercentage();
    bool chg = isCharging();
    displayShowRefillPrice(batt, chg, g_state.price_digits, idx,
                           g_state.refill_bag_count);

    uint32_t start = millis(), last_gesture = millis();
    bool validated = false;
    while ((millis() - start) < kRefillSessionTimeoutMs) {
        if (buttonTakeLong()) {
            if (idx >= kRefillPriceDigits - 1) {
                Serial.println("Price validated -> measure AFTER");
                validated = true;
                break;
            }
            idx++;
            g_state.price_idx = idx;
            Serial.printf("Long: next digit %u\n", idx);
            // Le cadre se déplace : on redessine tout l'écran (full).
            displayShowRefillPrice(batt, chg, g_state.price_digits, idx,
                                   g_state.refill_bag_count);
        }

        // Rafale : un refresh par clic pour que le montant progresse
        // visiblement au lieu de sauter à la valeur finale.
        uint8_t n = buttonTakeShorts();
        if (n) {
            displaySetKeepPowered(true);
            last_gesture = millis();
        }
        for (uint8_t i = 0; i < n; i++) {
            g_state.price_digits[idx] = (uint8_t)((g_state.price_digits[idx] + 1) % 10);
            displayShowRefillPriceUpdate(g_state.price_digits, idx,
                                         g_state.refill_bag_count);
        }
        if (!n && (millis() - last_gesture > 1500)) {
            displaySetKeepPowered(false);
        }
        delay(5);
    }
    if (!validated) {
        refillAbandon("price timeout");
        return;
    }
}

// Mesure « APRÈS » (delta = poids réel ajouté) puis l'écran de validation
// E14. Point d'entrée de la reprise après un dédock survenu avant cette mesure
// (RefillPhase::AFTER_MEASURE).
static void refillMeasureAfterAndConfirm() {
    Serial.println("=== Refill: measure AFTER ===");

    float before = g_state.refill_before_kg;
    float after = measureWeight();
    if (after < 0) {
        uint8_t vc = sensorsGetValidCount();
        if (vc == 0) {
            // CAS B — silo retiré avant la mesure « après » : les sacs sont
            // comptés et le prix saisi (RAM RTC), on reprend ici au redock.
            Serial.printf("Dedock before measure-after: %u sac(s) pending\n",
                          g_state.refill_bag_count);
            g_state.refill_pending = RefillPhase::AFTER_MEASURE;
            stateSetMode(ScaleMode::DEDOCKED);
            displayShowNomade(getBatteryPercentage(), isCharging());
            goToSleep(); // wake: timer (dedock long) / redock si contact
            return;
        }
        Serial.println("Refill aborted: sensors degraded after");
        enterErrorState(ScaleMode::DEGRADED, vc);
        return;
    }

    g_state.refill_pending = RefillPhase::NONE;

    float delta = after - before;
    Serial.printf("Refill delta: %.2f kg (before=%.2f after=%.2f), bags=%u\n",
                  delta, before, after, g_state.refill_bag_count);

    if (delta < 0.0f) {
        // Poids qui baisse alors qu'on a versé : incohérent → abandon (le cycle
        // normal publiera la baisse comme une consommation).
        refillAbandon("negative delta");
        return;
    }

    g_state.refill_delta_kg = delta;
}

// =============================================================================
// GESTE DE MAINTIEN (barre de 10 cases) — portail et ECRAN14
// =============================================================================
// Un seul comportement pour toutes les barres de maintien :
//   - TAP (relâché avant kHoldTapMaxMs) → l'action `● COURT` de l'écran ;
//   - TENUE complète (kHoldConfirmMs) → l'action `— LONG` ;
//   - tenue INTERROMPUE (relâché alors que la barre avait commencé) → RIEN : la
//     barre se vide et on reste sur l'écran (on peut retenter).
// La barre ne commence à se dessiner qu'après kHoldTapMaxMs : un partial bloque
// ~856 ms, le relâché d'un tap serait sinon daté trop tard. Tant que rien n'est
// dessiné, la durée de l'appui est donc mesurée au plus juste.
enum HoldGesture : uint8_t {
    HOLD_GESTURE_TAP = 0,   // appui court
    HOLD_GESTURE_DONE,      // maintien complet
    HOLD_GESTURE_TIMEOUT,   // aucun geste pendant la fenêtre
};

constexpr uint32_t kHoldTapMaxMs = 400UL;

// Un relâché n'est compté qu'après kHoldReleaseMs bouton levé : un rebond du
// contact ne doit ni compter comme un tap ni vider la barre.
constexpr uint32_t kHoldReleaseMs = 80UL;

// `show(pct)` redessine la barre (partial zoné) ; appelé avec 0 pour la vider.
// La fenêtre d'inactivité `idle_ms` repart de zéro à chaque tenue interrompue.
template <typename ShowFn>
static HoldGesture waitHoldGesture(uint32_t idle_ms, ShowFn show) {
    uint32_t idle_since = millis();
    bool holding = false;
    uint32_t hold_start = 0, up_since = 0;
    uint8_t shown_cells = 0;

    while ((millis() - idle_since) < idle_ms) {
        const bool down = readRefillButton();
        if (down) {
            up_since = 0;
            if (!holding) {
                holding = true;
                hold_start = millis();
                shown_cells = 0;
            }
            const uint32_t held = millis() - hold_start;
            if (held >= kHoldTapMaxMs) {
                const uint8_t pct = held >= kHoldConfirmMs
                                        ? 100
                                        : (uint8_t)((held * 100) / kHoldConfirmMs);
                const uint8_t cells = (uint8_t)((pct * kHoldBarCells) / 100);
                if (cells != shown_cells) {   // une case toutes les 10 %
                    shown_cells = cells;
                    show(pct);
                }
                if (pct >= 100) return HOLD_GESTURE_DONE;
            }
        } else if (holding) {
            if (up_since == 0) up_since = millis();
            if ((millis() - up_since) >= kHoldReleaseMs) {
                holding = false;
                buttonReset();   // le geste est décidé ici, pas par la file ISR
                if ((up_since - hold_start) < kHoldTapMaxMs) return HOLD_GESTURE_TAP;
                Serial.println("Hold: released before the end -> bar reset");
                if (shown_cells) show(0);
                shown_cells = 0;
                idle_since = millis();
            }
        }
        buttonTakeLong();   // la file ISR est ignorée : c'est la tenue qui décide
        buttonTakeShorts();
        delay(20);
    }
    return HOLD_GESTURE_TIMEOUT;
}

// ÉTAPE 4/5 — ECRAN14 « TERMINER ? » : le récap de ce qui va être enregistré,
// validé par un MAINTIEN de 2,5 s (la barre se remplit sous le doigt).
enum HoldOutcome : uint8_t {
    HOLD_ABANDON = 0,  // fenêtre de 10 min épuisée : abandon → refill sauvage
    HOLD_FIX,          // appui court : « CORRIGER ? »
    HOLD_SAVE,         // maintien complet : on enregistre
};

static HoldOutcome refillWaitHold(float delta, uint8_t bags, float total_eur) {
    switch (waitHoldGesture(kRefillSessionTimeoutMs, [&](uint8_t pct) {
                displayShowRefillConfirmHoldProgress(pct, delta, bags, total_eur);
            })) {
        case HOLD_GESTURE_DONE:
            Serial.println("Refill: hold confirmed -> save");
            return HOLD_SAVE;
        case HOLD_GESTURE_TAP:
            Serial.println("Refill: short press -> fix menu");
            return HOLD_FIX;
        default:
            Serial.println("Refill: confirm timeout");
            return HOLD_ABANDON;
    }
}

static void refillSave(float delta, uint8_t bags, float bag_price_eur,
                       float total_eur);

// « CORRIGER ? » : menu `● COURT SUIVANT` / `— LONG CHOISIR`, comme OPTIONS.
// Rend le choix ; FIX_COUNT = fenêtre de 10 min épuisée (l'appelant abandonne).
static RefillFixItem refillFixMenu() {
    // Le tap qui a quitté E14 est déjà jeté (waitHoldGesture) ; par sûreté.
    buttonReset();
    uint8_t sel = FIX_BAGS;
    displayShowRefillFix(sel, getBatteryPercentage(), isCharging());

    uint32_t t0 = millis(), last_gesture = t0;
    while ((millis() - t0) < kRefillSessionTimeoutMs) {
        if (buttonTakeLong()) {
            displaySetKeepPowered(false);
            Serial.printf("Refill fix: choice %u\n", sel);
            return (RefillFixItem)sel;
        }
        uint8_t n = buttonTakeShorts();
        if (n) {
            displaySetKeepPowered(true);   // rafale : _PowerOn payé une fois
            last_gesture = millis();
        }
        for (uint8_t i = 0; i < n; i++) {
            const uint8_t from = sel;
            sel = (uint8_t)((sel + 1) % FIX_COUNT);
            displayShowRefillFixSelection(from, sel);
        }
        if (!n && (millis() - last_gesture > 1500)) {
            displaySetKeepPowered(false);
        }
        delay(5);
    }
    displaySetKeepPowered(false);
    return FIX_COUNT;
}

// E14 en BOUCLE (et non en récursion) : « E14 → CORRIGER ? → E12/E13 → E14 »
// peut se répéter autant que l'utilisateur veut corriger, la pile reste constante.
// Le récap est RECALCULÉ à chaque tour : après une correction du prix et la
// re-mesure « après », E14 montre — et l'envoi porte — les valeurs à jour.
// Rien n'est publié sans un maintien complet sur le récap affiché.
static void refillConfirmAndSave() {
    for (;;) {
        const float bag_price_eur = g_state.price_digits[0]
                                  + g_state.price_digits[1] / 10.0f
                                  + g_state.price_digits[2] / 100.0f;
        const float delta = g_state.refill_delta_kg;
        const uint8_t bags = g_state.refill_bag_count;
        // Coût TOTAL = nb de sacs × prix du sac (fidèle à ce qu'on a payé). Le
        // poids mesuré (delta) est une VÉRIFICATION, pas la base du coût.
        const float total_eur = bags * bag_price_eur;
        Serial.printf("Refill confirm: +%.1f kg, %u sac(s) à %.2f € = %.2f EUR\n",
                      delta, bags, bag_price_eur, total_eur);

        // L'appui qui a validé le prix est déjà consommé ; on n'absorbe que s'il
        // est encore tenu (sinon le maintien du geste précédent validerait
        // d'emblée). Puis on dessine l'écran de validation.
        absorbWakePress();
        displayShowRefillConfirm(getBatteryPercentage(), delta, bags, total_eur,
                                 isCharging());

        switch (refillWaitHold(delta, bags, total_eur)) {
            case HOLD_SAVE:
                refillSave(delta, bags, bag_price_eur, total_eur);   // ne revient pas
                return;
            case HOLD_FIX:
                // Correction : retour à l'étape choisie, puis re-mesure « après »
                // (le silo a pu bouger). Ces étapes ne reviennent pas en cas
                // d'abandon, de dédock ou de capteurs KO.
                switch (refillFixMenu()) {
                    case FIX_BAGS:
                        // On RECOMPTE depuis 0 : le bouton ne fait que +1, un
                        // compte trop haut ne se corrigerait pas autrement. Le
                        // prix saisi est conservé (pas de repassage par E13).
                        g_state.refill_bag_count = 0;
                        publishRefillState("pouring");
                        stateSetMode(ScaleMode::REFILL_POURING);
                        refillStepBags();
                        refillMeasureAfterAndConfirm();
                        break;
                    case FIX_PRICE:
                        // Le curseur repart du 1er chiffre : sinon seul le
                        // dernier serait corrigeable.
                        g_state.price_idx = 0;
                        refillStepPrice();
                        refillMeasureAfterAndConfirm();
                        break;
                    case FIX_CANCEL:
                        // Rien de déclaré n'est publié ; le versement l'est en
                        // refill sauvage par le cycle normal.
                        refillAbandon("cancelled by user");   // ne revient pas
                        return;
                    case FIX_BACK:
                        break;             // → E14 inchangé
                    default:
                        refillAbandon("fix menu timeout");     // ne revient pas
                        return;
                }
                break;                 // → E14 avec les valeurs à jour
            default:
                refillAbandon("confirm timeout");   // ne revient pas
                return;
        }
    }
}

// ENREGISTREMENT puis ECRAN15 « ENREGISTRÉ ». Ne revient pas (deep sleep).
static void refillSave(float delta, uint8_t bags, float bag_price_eur,
                       float total_eur) {
    const float weight_after = g_state.refill_before_kg + delta;

    // --- ENREGISTREMENT -----------------------------------------------------
    // ⚠️ Bloc bloquant (WiFi + MQTT, quelques secondes si le broker est
    // injoignable) : l'utilisateur voit E14 avec sa barre pleine pendant ce
    // temps. C'est volontaire — on ne dit « ENREGISTRÉ » qu'après coup.
    Serial.printf("Publishing refill: +%.1f kg, %u sac(s) @ %.2f EUR\n",
                  delta, bags, bag_price_eur);
    uint8_t batt = getBatteryPercentage();
    bool sent = publishData(weight_after, batt);
    commitRefillPalier(weight_after, batt, sent);
    // L'événement (source de vérité comptable, PAS de retain) + l'état du
    // parcours (`saved` : le silo est TOUJOURS hors de sa base, on n'annonce
    // donc surtout pas « docked » ici — cf. publishRefillState()).
    if (wifiConnect() && mqttConnect()) {
        mqttPublishRefillState("saved");
        mqttPublishRefillEvent(delta, bag_price_eur, bags);
        mqttDisconnect();
    }
    wifiDisconnect();

    // --- ÉCRAN 5/5 : « ENREGISTRÉ », 1 minute --------------------------------
    displayShowRefillSaved(getBatteryPercentage(), delta, bags, total_eur,
                           weight_after, isCharging());
    Serial.println("=== Refill: SAVED (1 min on screen) ===");

    // Le seul geste annoncé est `● COURT PRINCIPAL` ; l'appui long en est un
    // alias (une carte posée là ne doit pas devenir un bouton mort). Sans geste,
    // l'écran reste 1 minute puis on rejoint l'écran principal.
    const uint32_t kSavedDwellMs = 60000UL;
    uint32_t dwell_start = millis();
    while ((millis() - dwell_start) < kSavedDwellMs) {
        if (buttonTakeShorts() || buttonTakeLong()) {
            Serial.println("Saved: leaving early");
            break;
        }
        delay(20);
    }

    stateSetMode(ScaleMode::DOCKED);
    // Écran principal avec le poids DÉJÀ mesuré : on ne remesure pas (le
    // poids vient d'être mesuré et publié), puis deep sleep normal.
    displayShowMain(weight_after, getBatteryPercentage(), isCharging());
    goToSleep(); // mode DOCKED → prochain créneau
}

// =============================================================================
// CONFIRMATION DE DÉMARRAGE DU PORTAIL (maintien = lancement)
// =============================================================================
// Le portail ouvre un point d'accès WiFi (protégé par un mot de passe affiché sur
// l'e-paper) : le démarrage doit être un geste délibéré. L'écran de confirmation
// affiche une barre de maintien ; un appui court QUITTE, seule la tenue du bouton
// LANCE (le portail, qui ne rend pas la main), une tenue interrompue vide la
// barre (cf. waitHoldGesture). Sans appui pendant kPortalConfirmTimeoutMs,
// l'écran est abandonné.
//
// kHoldConfirmMs (2,5 s) est défini plus haut : il est PARTAGÉ avec l'écran
// « TERMINER ? » du remplissage (ECRAN14), les deux étant des « je valide en
// tenant ». kPortalConfirmTimeoutMs reste propre au portail.
constexpr uint32_t kPortalConfirmTimeoutMs = 30000; // abandon sans appui

// Affiche la confirmation puis attend le geste. Rend quand l'utilisateur a
// quitté (aucun écran n'est redessiné : l'appelant décide, ex. retour au menu).
static void appPortalConfirm() {
    enterInteractiveSession();   // capture des appuis + absorption de l'appui d'arrivée

    displayShowPortalConfirm(getBatteryPercentage(), isCharging());

    switch (waitHoldGesture(kPortalConfirmTimeoutMs,
                            [](uint8_t pct) { displayShowPortalHoldProgress(pct); })) {
        case HOLD_GESTURE_DONE:
            Serial.println("Portal: hold confirmed -> webConfigRun");
            webConfigRun();   // ne revient pas (redémarrage en sortie)
            return;
        case HOLD_GESTURE_TAP:
            Serial.println("Portal: short press -> quit");
            return;
        default:
            break;
    }
    Serial.println("Portal: confirmation timeout -> quit");
}

// =============================================================================
// MENU OPTIONS (boucle interactive)
// =============================================================================
// `● COURT SUIVANT` = la barre de sélection descend (partial zoné, sans flash),
// `— LONG CHOISIR` = l'entrée sélectionnée est validée. Le menu a une issue
// EXPLICITE (`FERMER`) : sans elle, on n'en sortirait qu'en choisissant une
// action — ce qui n'est pas un menu.
//
// Filet de sécurité : sans geste pendant kMenuIdleMs, on sort comme par FERMER
// (l'utilisateur s'est éloigné). Non annoncé au pied : le pied annonce les
// gestes, pas le lack d'attention.
constexpr uint32_t kMenuIdleMs = 60000UL; // 1 min sans geste

// Affiche les 2 pages d'informations enchaînées : `● COURT` = page suivante,
// `— LONG` = retour au menu. Ne retourne pas (le retour se fait au `● COURT` de
// la 2e page : c'est la dernière, il n'y a rien après).
//
// Les valeurs affichées sont RÉELLES : on ouvre une session réseau pour le
// WiFi / le broker / le signal, et on lit les 4 pieds (une lecture par pied,
// sans filtrage) pour la répartition. Un « OK » inventé serait un mensonge.
static void appShowInformations() {
    uint8_t batt = getBatteryPercentage();
    bool chg = isCharging();
    bool wifi_ok = wifiConnect();
    bool mqtt_ok = wifi_ok && mqttConnect();
    int16_t rssi = wifiRSSI();
    if (mqtt_ok) mqttDisconnect();
    wifiDisconnect();

    // Répartition : une lecture par pied (le cycle qui a établi la session a fait
    // sensorsInit ; on le refait pour être sûr d'avoir des HX711 prêts — un pied
    // non disponible affiche « -- »).
    sensorsInit();
    // Compté ICI : sensorsGetValidCount() est celui de la dernière mesure
    // complète, absente si le menu a été ouvert par un réveil bouton (→ « 0 OK »).
    float feet[4];
    uint8_t valid = 0;
    for (uint8_t i = 0; i < 4; i++) {
        float raw = 0.0f, kg = 0.0f;
        const bool ok = sensorsReadFoot(i, raw, kg, /*samples=*/1);
        feet[i] = ok ? kg : -1.0f;
        valid += ok;
    }

    // Les DEUX pages annoncent les mêmes deux gestes :
    //   `● COURT AUTRE PAGE` bascule page 1 ↔ page 2, autant de fois que voulu
    //     (c'est une consultation : on revient dessus, on ne « avance » pas) ;
    //   `— LONG RETOUR`      rend la main au menu.
    // Sans geste pendant kMenuIdleMs, on rend aussi la main : un utilisateur qui
    // s'éloigne ne doit pas rester bloqué sur l'écran nomade.
    uint8_t page = 0;
    displayShowInfosGeneral(batt, chg, wifi_ok, mqtt_ok, rssi, valid);

    const uint32_t t0 = millis();
    uint32_t last_gesture = t0;
    while ((millis() - t0) < kMenuIdleMs) {
        if (buttonTakeLong()) {
            Serial.println("Infos: long -> menu");
            return;
        }
        if (buttonTakeShorts()) {
            page = (uint8_t)(page ^ 1);
            Serial.printf("Infos: -> page %u\n", page + 1);
            if (page == 0) {
                displayShowInfosGeneral(batt, chg, wifi_ok, mqtt_ok, rssi, valid);
            } else {
                displayShowInfosSensors(batt, chg, feet, valid);
            }
            last_gesture = millis();
        }
        if ((millis() - last_gesture > 1500)) displaySetKeepPowered(false);
        delay(5);
    }
    Serial.println("Infos: timeout -> menu");
}

// Sortie du menu (`FERMER` comme le filet d'inactivité) : retour à l'écran
// correspondant au mode, SANS remesure, puis deep sleep.
//
//   - posé sur la base → écran PRINCIPAL avec le dernier poids connu (-1 si
//     aucune mesure n'a jamais été faite) ;
//   - hors base → écran MODE NOMADE. On ne peut pas afficher l'écran principal : il
//     montre une jauge de silo et un poids, et il n'y a aucune mesure possible
//     ici — « CONNECTÉ » serait un mensonge.
//
// Le poids affiché est donc toujours celui déjà mesuré : c'est le comportement
// demandé (« le retour ne re-déclenche pas de mesure »), et il n'y a rien à
// publier non plus.
static void appCloseToIdle() {
    if (stateGetMode() == ScaleMode::DEDOCKED) {
        displayShowNomade(getBatteryPercentage(), isCharging());
        goToSleep();   // mode inchangé : veille nomade (poll de charge)
        return;
    }
    stateSetMode(ScaleMode::DOCKED);
    displayShowMain(g_state.has_last_measure ? g_state.last_measure_kg : -1.0f,
                   getBatteryPercentage(), isCharging());
    goToSleep();
}

// Ouvre le menu et ne retourne pas (toutes les sorties dorment ou lancent le
// portail). Le remplissage est le seul choix qui ne rend pas la main : les
// autres affichent un écran puis reviennent au menu, SAUF `FERMER` qui dort.
static void appOptionsMenu() {
    enterInteractiveSession(); // capture des appuis + absorption de l'appui d'arrivée

    // Le curseur se pose sur la première ligne JOUABLE : `REMPLISSAGE` si le silo
    // est sur sa balance, sinon `PORTAIL RÉGLAGES` (jamais sur une ligne tramée).
    uint8_t sel = menuFirstEnabledRow();
    const uint8_t rows = menuRowCount();   // toujours 4
    Serial.printf("=== Menu OPTIONS (%u entrees, curseur sur '%s') ===\n", rows,
                  menuRowEnabled(sel) ? "ligne active" : "ligne inactive");
    displayShowOptions(sel, getBatteryPercentage(), isCharging());

    uint32_t t0 = millis();
    uint32_t last_gesture = t0;
    while ((millis() - t0) < kMenuIdleMs) {
        if (buttonTakeLong()) {
            switch (menuRowItem(sel)) {
                case MENU_REFILL:
                    // Le curseur ne s'arrête jamais sur cette ligne hors base
                    // (elle est tramée et sautée) : on est donc sur une base posée.
                    if (!menuRowEnabled(sel)) {
                        Serial.println("Menu: refill indisponible (ligne inactive)");
                        break;             // on redessine le menu
                    }
                    Serial.println("Menu: refill");
                    refillStart();        // ne retourne pas (dort à la fin)
                    return;
                case MENU_PORTAL:
                    Serial.println("Menu: portal (confirmation)");
                    // Rend si l'utilisateur renonce (court / timeout) : on
                    // redessine le menu. Ne revient pas s'il a tenu 2,5 s.
                    appPortalConfirm();
                    break;
                case MENU_INFOS:
                    Serial.println("Menu: informations");
                    // Revenir des informations RAMÈNE AU MENU : c'est une
                    // consultation, pas une sortie. Le `return` sortait
                    // d'appOptionsMenu() et la carte allait dormir — le menu
                    // disparaissait alors que l'écran annonçait « LONG RETOUR ».
                    // Le curseur repart sur la même ligne (INFORMATIONS), donc
                    // on peut enchaîner consultation après consultation.
                    appShowInformations();
                    break;
                default:  // MENU_CLOSE
                    Serial.println("Menu: close -> ecran du mode");
                    appCloseToIdle();      // ne retourne pas
                    return;
            }
            // On retombe ici si l'action a été quittée : le menu est toujours
            // affiché (la confirmation n'a rien laissé dessus), on reprend.
            displayShowOptions(sel, getBatteryPercentage(), isCharging());
            last_gesture = millis();
            continue;
        }

        // `● COURT SUIVANT` : une ligne par clic. Une rafale de N clics fait
        // donc N lignes (avec partial à chaque, la barre « descend » vraiment).
        uint8_t n = buttonTakeShorts();
        for (uint8_t i = 0; i < n; i++) {
            displaySetKeepPowered(true); // rafale : _PowerOn payé une fois
            last_gesture = millis();
            const uint8_t from = sel;
            sel = menuNextEnabledRow(from);   // saute les lignes inactives
            displayShowOptionsSelection(from, sel, getBatteryPercentage(),
                                        isCharging());
        }
        if (!n && (millis() - last_gesture > 1500)) {
            displaySetKeepPowered(false); // plus de geste : on coupe la dalle
        }
        delay(5);
    }

    Serial.println("Menu: idle timeout -> close");
    appCloseToIdle();
}

// =============================================================================
// DISPATCH PAR CAUSE DE REVEIL
// =============================================================================

static void appHandleExternalWake() {
    // Réveil par GPIO. Deux sources possibles : ext1 (bouton de remplissage) et
    // ext0 (ligne DOUT dedock/redock, si USE_DOCK_WAKE_DOUT). Le statut ext1
    // (esp_sleep_get_ext1_wakeup_status) peut être 0 avec ALL_LOW sur cette IDF
    // → on se fie aussi à l'état RÉEL des pins au réveil.
#if USE_DOCK_WAKE_DOUT
    if (dockWakeupExt0()) {
        // Ligne DOUT : le niveau donne la direction, mais on enchaîne un cycle
        // de mesure — il CONFIRME l'état par les capteurs (0/4 → dédock, 4/4 →
        // redock) et publie s'il y a changement, plutôt que de se fier au seul
        // niveau de la ligne.
        powerReleaseSensorHold(); // libère les SCK tenues + rend la pad DOUT au GPIO
        Serial.printf("Wake source: DOUT %s\n",
                      readDockSignal() ? "dedock" : "redock");
        // Le redock doit être vu tout de suite (le silo vient d'être reposé) :
        // jamais un poll « charge seule », d'où force_full.
        appRunScheduledCycle(/*force_full=*/true);
        return;
    }
#endif

    const bool btn_pressed = readRefillButton();
    const bool btn_wake = refillButtonWoke() || btn_pressed;

    if (btn_wake) {
        Serial.println("Wake source: Refill button");
        switch (stateGetMode()) {
            case ScaleMode::DOCKED:
            case ScaleMode::DEGRADED:
                // C'est ce que le pied de l'écran principal annonce :
                //   `● COURT MESURER` → mesure à la demande (cycle normal
                //   complet, publication MQTT si changement) ;
                //   `— LONG OPTIONS`  → ouvre le menu.
                // Le remplissage n'est plus un appui long direct : il se lance
                // depuis `REMPLISSAGE` du menu, donc chaque étape du parcours
                // (versement, comptage, prix, validation) est choisie
                // explicitement. C'est plus long à atteindre, mais un geste
                // unique ne déclenche plus une saisie de prix.
                if (waitLongPress(5000)) {
                    appOptionsMenu();  // ne retourne pas
                    return;
                }
                Serial.println("Short press: on-demand measure");
                stateSetMode(ScaleMode::DOCKED);
                appRunScheduledCycle(); // mesure + affichage + MQTT si Δ
                return;
            case ScaleMode::REFILL_POURING:
            case ScaleMode::REFILL_PRICING:
                // Parcours interrompu (cf. appRunScheduledCycle) : abandon.
                refillAbandon("interrupted (button wake)");
                return;
            case ScaleMode::DEDOCKED:
                // Écran « MODE NOMADE » : un seul geste, `— LONG OPTIONS` (le
                // menu, comme sur l'écran principal). Hors base, rien à mesurer
                // ni à relancer : l'appui court n'est pas annoncé, il ne fait
                // donc rien — on se rendort, l'écran nomade reste affiché.
                if (waitLongPress(1500)) {
                    appOptionsMenu();  // ne retourne pas (dort à la sortie)
                    return;
                }
                // Appui court : pas de geste annoncé, mais on en profite pour
                // relire la charge (utile juste après avoir branché l'USB :
                // hors charge, le prochain réveil timer est à 00:02).
                Serial.println("Dedock: short press -> charge check");
                appRunNomadeLightPoll();   // ne revient pas
                return;
            default:
                goToSleep();
                break;
        }
    } else {
        Serial.println("Wake source: External (unknown pin)");
        goToSleep();
    }
}

// Carte SANS configuration réseau : aucun cycle de mesure n'est possible. Elle
// dort, ne se réveille (timer) que pour vérifier sa batterie, et le bouton
// rouvre le portail. Plus de portail relancé toutes les 10 min : c'était un AP
// allumé en continu, sans aucune garde batterie.
static void appUnconfiguredSleep() {
    const float vbat = readBatteryVoltage();
    const uint32_t sec = (vbat < BATTERY_SHUTDOWN_THRESHOLD_V) ? kCriticalSleepSec
                                                               : kUnconfiguredSleepSec;
    Serial.printf("Unconfigured: battery %.2fV, sleeping %lus (button = portal)\n",
                  vbat, (unsigned long)sec);
    // Écran « CARTE EN VEILLE » une seule fois : les réveils timer suivants
    // (contrôle batterie) le retrouvent déjà affiché, l'e-paper le garde.
    if (g_state.disp_screen != DisplayScreen::UNCONFIGURED) {
        displayShowUnconfiguredSleep(getBatteryPercentage(), isCharging());
    }
    sensorsPowerOff();
    buttonEnable(false);
    displaySetKeepPowered(false);
    displayPanelDeepSleep();
    armRefillWakeup();
    prepareDeepSleep(sec);
    enterDeepSleep();
}

void appRun() {
    WakeCause cause = powerGetWakeCause();
#if USE_CHRG_ULP
    // L'ULP de la veille nomade tourne encore (sauf s'il a lui-même réveillé la
    // carte) : on l'arrête, seul goToSleepFor() le relance, et seulement en
    // nomade. Les chemins qui dorment sans lui (batterie critique, carte non
    // configurée) ne le laissent donc pas tourner.
    disarmNomadeUlpWakeup();
#endif

    // Portail de configuration : deux seules entrées.
    //  - carte SANS réseau/broker enregistré (tout le reste a des valeurs par
    //    défaut) : au démarrage et à l'appui du bouton. À la fermeture du
    //    portail sans enregistrement, elle dort (appUnconfiguredSleep) ; un
    //    réveil timer ne fait que vérifier la batterie ;
    //  - sur DEMANDE, depuis « PORTAIL RÉGLAGES » du menu OPTIONS, après la
    //    page de confirmation (appui maintenu) — voir appPortalConfirm().
    if (!settingsHasStored()) {
        if (cause == WakeCause::TIMER) appUnconfiguredSleep();   // ne revient pas
        Serial.println("Aucun reseau/broker enregistre -> portail de configuration");
        webConfigRun();          // revient si rien n'a été enregistré
        appUnconfiguredSleep();  // ne revient pas
    }

    switch (cause) {
        case WakeCause::TIMER:
            if (!appDockedDateWake()) appRunScheduledCycle();
            break;
        case WakeCause::GPIO:
            appHandleExternalWake();
            break;
        case WakeCause::ULP:
            if (ulpWokeForRedock()) {
                // Boîtier reposé : comme le réveil ext0, un cycle COMPLET qui
                // confirme par la mesure (jamais un poll « charge seule »).
                Serial.println("Wake source: DOUT redock (ULP)");
                powerReleaseSensorHold();
                appRunScheduledCycle(/*force_full=*/true);
            } else {
                // CHRG a changé (USB branché, ou charge finie) : poll léger —
                // l'éclair suit, et le sommeil repasse à 5 min en charge /
                // 00:02 sinon.
                Serial.println("Wake source: CHRG (ULP)");
                appRunScheduledCycle();
            }
            break;
        case WakeCause::COLD_BOOT:
        default:
            Serial.println("Wake source: Cold boot / reset");
            appRunScheduledCycle(/*force_full=*/true);
            break;
    }
}
