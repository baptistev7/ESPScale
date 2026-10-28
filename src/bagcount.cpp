#include "bagcount.hpp"

#include <math.h>

uint16_t bagCountFromDelta(float delta_kg, float bag_min_kg) {
    if (bag_min_kg <= 0.0f || delta_kg + kBagMarginKg < bag_min_kg) return 0;
    return (uint16_t)floorf((delta_kg + kBagMarginKg) / bag_min_kg);
}

static void applyStep(BagCounter& c, float delta_kg) {
    c.acc_kg += delta_kg;
    if (c.acc_kg < 0.0f) c.acc_kg = 0.0f;   // une baisse annule un appui, pas un sac
    const uint16_t n = bagCountFromDelta(c.acc_kg, c.bag_min_kg);
    if (n) {
        c.bags += n;
        c.acc_kg = 0.0f;
    }
}

void bagCounterInit(BagCounter& c, float start_kg, uint32_t now_ms, float bag_min_kg) {
    c.bag_min_kg = bag_min_kg;
    c.ref_kg = start_kg;
    c.cand_kg = start_kg;
    c.cand_since_ms = now_ms;
    c.committed = true;      // le point de départ est un palier
    c.acc_kg = 0.0f;
    c.bags = 0;
}

void bagCounterFeed(BagCounter& c, float kg, uint32_t now_ms) {
    if (fabsf(kg - c.cand_kg) > kBagPlateauEpsKg) {
        c.cand_kg = kg;               // ça bouge : nouvelle stabilité à attendre
        c.cand_since_ms = now_ms;
        c.committed = false;
        return;
    }
    if (!c.committed && (now_ms - c.cand_since_ms) >= kBagPlateauMs) {
        applyStep(c, c.cand_kg - c.ref_kg);
        c.ref_kg = c.cand_kg;
        c.committed = true;
    }
}

uint16_t bagCounterFinish(BagCounter& c, float kg) {
    applyStep(c, kg - c.ref_kg);
    c.ref_kg = kg;
    c.cand_kg = kg;
    c.committed = true;
    return c.bags;
}

bool bagCounterStable(const BagCounter& c, uint32_t now_ms) {
    return (now_ms - c.cand_since_ms) >= kBagPlateauMs;
}
