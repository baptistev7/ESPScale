#include "state.hpp"

// =============================================================================
// ETAT PERSISTANT (RAM RTC)
// =============================================================================

// Instance unique en RAM RTC — survit au deep sleep, réinitialisée à froid.
RTC_DATA_ATTR ScaleState g_state;

void stateReset() {
    g_state.magic            = STATE_MAGIC;
    g_state.mode             = ScaleMode::DOCKED;
    g_state.has_last_weight  = false;
    g_state.last_weight_kg   = 0.0f;
    g_state.last_battery_pct = 0;
    g_state.last_send_ok     = true;
    g_state.skip_count       = 0;
    g_state.refill_before_kg = 0.0f;
    g_state.refill_delta_kg  = 0.0f;
    g_state.price_digits[0]  = 0;
    g_state.price_digits[1]  = 0;
    g_state.price_digits[2]  = 0;
    g_state.price_idx        = 0;
    g_state.refill_bag_count = 0;
    g_state.refill_started_at = 0;
    g_state.refill_pending   = RefillPhase::NONE;
    g_state.low_batt_count   = 0;
    g_state.announced_error  = ScaleMode::DOCKED;
    g_state.disp_screen      = DisplayScreen::UNKNOWN;
    g_state.disp_battery     = 0;
    g_state.disp_charging    = false;
    g_state.disp_partial_run = 0;
    g_state.disp_drawn_at    = 0;
    Serial.println("[state] reset (cold boot)");
}

void stateInit() {
    if (g_state.magic != STATE_MAGIC) {
        // 1er boot ou power-on : RAM RTC non initialisée.
        stateReset();
        return;
    }
    // Layout valide : on garde l'état. On s'assure seulement que le mode est
    // cohérent (valeurs hors enum possibles si corruption).
    if (g_state.mode > ScaleMode::REFILL_PRICING) {
        Serial.println("[state] invalid mode, reset");
        stateReset();
    }
}

void stateSetMode(ScaleMode new_mode) {
    if (g_state.mode != new_mode) {
        Serial.printf("[state] mode: %s -> %s\n",
                      stateModeName(g_state.mode), stateModeName(new_mode));
        g_state.mode = new_mode;
    }
}

ScaleMode stateGetMode() {
    return g_state.mode;
}

const char* stateModeName(ScaleMode mode) {
    switch (mode) {
        case ScaleMode::DOCKED:         return "docked";
        case ScaleMode::DEDOCKED:       return "dedocked";
        case ScaleMode::DEGRADED:       return "degraded";
        case ScaleMode::REFILL_POURING: return "pouring";
        case ScaleMode::REFILL_PRICING: return "pricing";
        default:                        return "?";
    }
}
