#pragma once
#include <stdint.h>

// =============================================================================
// COMPTAGE AUTOMATIQUE DES SACS (docs/SPEC-1.1.md §5)
// =============================================================================
// Le poids est lu à intervalles réguliers pendant le versement. Un PALIER est un
// poids qui ne bouge plus de kBagPlateauEpsKg pendant kBagPlateauMs. La hausse
// entre deux paliers s'ajoute à un cumul ; dès qu'il atteint un sac, on compte
// floor((cumul + marge) / poids_min_sac) sacs et on repart de zéro : le surpoids
// des sacs est jeté. Un sac ne pesant jamais moins que `bag_min_kg`, le plancher
// reste exact tant qu'un palier n'a pas plus de ~14 sacs avec +1 kg chacun.
//
// Module pur (aucune dépendance matérielle) : testé sur PC par
// tools/bagcount_test.cpp.

// Constantes estimées, à affiner sur le matériel (SPEC-1.1 §5.3).
constexpr float    kBagPlateauEpsKg = 0.4f;   // tolérance d'un palier
constexpr uint32_t kBagPlateauMs    = 2000;   // durée de stabilité d'un palier
constexpr float    kBagMarginKg     = 0.5f;   // bruit + dérive, ajouté au plancher

struct BagCounter {
    float    bag_min_kg;      // poids minimum d'un sac
    float    ref_kg;          // dernier palier pris en compte
    float    cand_kg;         // début de la stabilité en cours
    uint32_t cand_since_ms;
    bool     committed;       // ce palier est déjà compté
    float    acc_kg;          // hausse cumulée pas encore convertie en sacs
    uint16_t bags;
};

void     bagCounterInit(BagCounter& c, float start_kg, uint32_t now_ms, float bag_min_kg);
void     bagCounterFeed(BagCounter& c, float kg, uint32_t now_ms);
// Fin du versement : `kg` est la mesure stable finale, comptée comme un palier.
uint16_t bagCounterFinish(BagCounter& c, float kg);
bool     bagCounterStable(const BagCounter& c, uint32_t now_ms);
// Même règle appliquée au seul delta total (reprise après un dédock : les paliers
// sont perdus).
uint16_t bagCountFromDelta(float delta_kg, float bag_min_kg);
