#include "button.hpp"

#include <Arduino.h>

#include "pins.hpp"
#include "power.hpp" // readRefillButton()

// =============================================================================
// ENREGISTREUR DE GESTES — ÉCHANTILLONNAGE PAR TIMER MATÉRIEL (1 kHz)
// =============================================================================
// L'ISR du timer fait tout : anti-rebond temporel, détection des fronts et
// classification court/long. Le programme principal ne fait que consommer les
// compteurs. Comme l'ISR est prioritaire et indépendante, elle tourne même
// pendant les blocages longs (BUSY de la dalle, WiFi/MQTT, capteurs).

// Durées de classification.
static const uint32_t kDebounceMs  = 20;   // stabilité exigée entre deux fronts
static const uint32_t kLongPressMs = 1000; // au-delà : appui long
static const uint32_t kMinClickMs  = 30;   // en dessous : bruit résiduel

// Échantillonnage : 1 kHz (80 MHz / 80 = 1 MHz ; alarme toutes les 1000 ticks).
static const uint16_t kTimerDivider = 80;
static const uint32_t kTimerPeriodTicks = 1000;

static hw_timer_t* s_timer = nullptr;
static volatile bool s_enabled = false;

// Machine à états (brut → stable après anti-rebond).
static volatile bool     s_raw = false;
static volatile uint32_t s_raw_changed_ms = 0;
static volatile bool     s_pressed = false;
static volatile uint32_t s_pressed_since_ms = 0;

// Gestes terminés, en attente de consommation.
static volatile uint8_t s_shorts_pending = 0;
static volatile bool    s_long_pending = false;

static void IRAM_ATTR buttonTimerIsr() {
    if (!s_enabled) return;

    const bool raw = readRefillButton();
    const uint32_t now = millis();

    // Mémorise le dernier changement, sans rien valider encore.
    if (raw != s_raw) {
        s_raw = raw;
        s_raw_changed_ms = now;
        return;
    }
    // Anti-rebond : l'état doit être stable depuis kDebounceMs.
    if ((now - s_raw_changed_ms) < kDebounceMs) return;
    // Pas de nouveau front stable : rien à faire.
    if (raw == s_pressed) return;

    s_pressed = raw;
    if (raw) {
        // FRONT MONTANT accepté : début de l'appui.
        s_pressed_since_ms = now;
        return;
    }

    // FRONT DESCENDANT accepté : l'appui est terminé, on le classe.
    const uint32_t dur = now - s_pressed_since_ms;
    if (dur >= kLongPressMs) {
        s_long_pending = true;
    } else if (dur >= kMinClickMs) {
        if (s_shorts_pending < 255) s_shorts_pending++;
    }
    // En dessous de kMinClickMs : bruit, ignoré.
}

void buttonEnable(bool on) {
    if (on) {
        if (s_timer == nullptr) {
            s_timer = timerBegin(0, kTimerDivider, true); // 1 MHz
            timerAttachInterrupt(s_timer, &buttonTimerIsr, true);
            timerAlarmWrite(s_timer, kTimerPeriodTicks, true); // 1 kHz
        }
        buttonReset();
        s_enabled = true;
        timerAlarmEnable(s_timer);
    } else {
        s_enabled = false;
        if (s_timer != nullptr) timerAlarmDisable(s_timer);
        buttonReset();
    }
}

void buttonReset() {
    noInterrupts();
    s_shorts_pending = 0;
    s_long_pending = false;
    const bool level = readRefillButton();
    s_raw = level;
    s_raw_changed_ms = millis();
    s_pressed = level;
    s_pressed_since_ms = millis();
    interrupts();
}

uint8_t buttonTakeShorts() {
    noInterrupts();
    const uint8_t n = s_shorts_pending;
    s_shorts_pending = 0;
    interrupts();
    return n;
}

bool buttonTakeLong() {
    noInterrupts();
    const bool l = s_long_pending;
    s_long_pending = false;
    interrupts();
    return l;
}
