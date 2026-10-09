#include "sensors.hpp"
#include "pins.hpp"
#include "power.hpp"     // powerReleaseSensorHold() (pads SCK tenues en sleep)
#include "settings.hpp" // calibration persistée (settingsGetCal)

// =============================================================================
// CAPTEURS GLOBAUX
// =============================================================================

static HX711 scale_1;
static HX711 scale_2;
static HX711 scale_3;
static HX711 scale_4;

static HX711* scales[4] = {&scale_1, &scale_2, &scale_3, &scale_4};

// Capteurs valides au dernier sensorsReadTotalWeight() (0-4)
static uint8_t g_valid_sensors = 0;
// Masque des capteurs prêts au dernier sensorsInit() (bit i = capteur i)
static uint8_t g_ready_mask = 0;

uint8_t sensorsGetValidCount() {
    return g_valid_sensors;
}

// Configuration des broches pour chaque capteur (registre unique pins.hpp)
static const gpio_num_t dout_pins[4] = {
    pins::LC1_DOUT,
    pins::LC2_DOUT,
    pins::LC3_DOUT,
    pins::LC4_DOUT
};

static const gpio_num_t sck_pins[4] = {
    pins::LC1_SCK,
    pins::LC2_SCK,
    pins::LC3_SCK,
    pins::LC4_SCK
};

// =============================================================================
// IMPLEMENTATION
// =============================================================================

// Applique la calibration courante (NVS, défauts compilés si absente) aux objets
// HX711. Appelée à l'init ET à chaud par le portail après un enregistrement.
void sensorsApplyCalibration() {
    const Calibration& cal = settingsGetCal();
    for (uint8_t i = 0; i < 4; i++) {
        scales[i]->set_scale(cal.c[i]);
        scales[i]->set_offset(cal.z[i]);
    }
}

void sensorsInit() {
    Serial.println("Initializing HX711 sensors...");

    // Au réveil d'un deep sleep, les SCK sont tenues hautes par le domaine RTC
    // (voir prepareDeepSleep). On rend les pads au chemin numérique AVANT
    // begin() : tant qu'elles appartiennent au RTC, pinMode()/digitalWrite()
    // n'ont pas prise dessus (l'HX711 resterait éteint / muet).
    powerReleaseSensorHold();

    uint32_t t0 = millis();

    // Configure les broches de chaque capteur et applique la calibration
    for (uint8_t i = 0; i < 4; i++) {
        scales[i]->begin((uint8_t)dout_pins[i], (uint8_t)sck_pins[i]);
    }
    sensorsApplyCalibration();
    Serial.printf("  begin: %lums\n", (unsigned long)(millis() - t0));

    // Sort les HX711 de leur power-down (maintenu pendant le deep sleep)
    sensorsPowerOn();
    Serial.printf("  poweron: %lums\n", (unsigned long)(millis() - t0));

    // Pull-up sur les DOUT PENDANT la lecture : un DOUT non branché (capteur
    // absent) est tiré HIGH → is_ready() reste false (il attend LOW). Sans ça,
    // le pin flotte et is_ready() renvoie true aléatoirement → faux "0kg".
    // Les pull-ups sont recoupés en deep sleep (voir prepareDeepSleep).
    for (uint8_t i = 0; i < 4; i++) {
        gpio_pullup_en(dout_pins[i]);
    }

    // Attend que TOUS les capteurs soient prêts sous une deadline globale
    // (un HX711 à 10Hz produit une conversion toutes les ~100ms, donc en
    // pratique tous sont prêts en ~200ms ; le timeout ne sert qu'au cas où
    // un capteur est débranché). Bien plus rapide que d'attendre chaque
    // capteur séquentiellement.
    const uint32_t start_ms = millis();
    uint8_t ready_mask = 0;

    while (ready_mask != 0x0F && (millis() - start_ms) < SENSOR_READY_TIMEOUT_MS) {
        for (uint8_t i = 0; i < 4; i++) {
            if (!(ready_mask & (1u << i)) && scales[i]->is_ready()) {
                ready_mask |= (1u << i);
                Serial.printf("Scale %d ready (%lums)\n", i + 1,
                             (unsigned long)(millis() - start_ms));
            }
        }
        delay(5);
    }
    g_ready_mask = ready_mask;
    Serial.printf("  ready wait: %lums (mask=%u)\n",
                  (unsigned long)(millis() - t0), ready_mask);

    // Rapporte les capteurs qui n'ont jamais répondu
    for (uint8_t i = 0; i < 4; i++) {
        if (!(ready_mask & (1u << i))) {
            Serial.printf("Error: Scale %d not ready!\n", i + 1);
        }
    }
}

float sensorsReadTotalWeight(uint8_t attempts) {
    float total_weight = 0.0f;
    uint8_t valid_sensors = 0;
    float weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    uint8_t valid_reads[4] = {0, 0, 0, 0}; // lectures valides par capteur

    // ATTENTION : get_units() → read() → wait_ready() est une boucle INFINIE
    // sur un capteur absent (DOUT qui reste HIGH). On ne lit QUE les capteurs
    // confirmés prêts à l'init (g_ready_mask) — sinon on sort immédiatement
    // (dedock) sans jamais risquer le blocage.
    if (g_ready_mask == 0) {
        g_valid_sensors = 0;
        Serial.println("No sensor ready at init, skipping reads (dedock)");
        return -1.0f;
    }

    for (uint8_t attempt = 0; attempt < attempts; attempt++) {
        for (uint8_t i = 0; i < 4; i++) {
            if (!(g_ready_mask & (1u << i))) continue; // jamais prêt à l'init
            HX711* scale = scales[i];
            // Anti-blocage : wait_ready() seul boucle INDÉFINIMENT si le DOUT
            // reste HIGH (capteur débranché / mort en cours de cycle). On borne
            // l'attente : wait_ready_timeout(ms, délai) → false si absent.
            // 500 ms par lecture est largement suffisant (conversion à 10 Hz).
            if (scale->wait_ready_timeout(500, 1)) {
                float reading = scale->get_units(1);
                // Filtre de plausibilité : borne PHYSIQUE du pied (4 cellules de
                // 50 kg = 200 kg). Au-delà, le pied sature : la valeur est
                // impossible et doit être rejetée (le pied devient invalide, donc
                // la mesure 4/4 échoue → aucune publication) plutôt que d'être
                // publiée comme une hausse légitime (faux refill sauvage).
                if (reading >= -FOOT_CAPACITY_KG && reading <= FOOT_CAPACITY_KG) {
                    weights[i] += reading;
                    valid_reads[i]++;
                }
            }
        }
        if (attempt + 1 < attempts) delay(SENSOR_READ_DELAY_MS);
    }

    for (uint8_t i = 0; i < 4; i++) {
        // Capteur jamais prêt / aucune lecture valide → invalide.
        if (valid_reads[i] == 0) {
            Serial.printf("Warning: Invalid reading from scale %d\n", i + 1);
            continue;
        }
        float weight = weights[i] / valid_reads[i];
        total_weight += weight;
        valid_sensors++;
    }
    
    // Mémorise le nombre de capteurs valides (pour détecter le dedock)
    g_valid_sensors = valid_sensors;
    
    // STRICT 4/4 : on exige que les 4 capteurs soient valides pour publier un
    // poids. L'extrapolation "moyenne × 4" supposait une charge équitable des
    // 4 pieds — fausse si le silo est déséquilibré ou un pied décollé → elle
    // masquait une erreur réelle. Pour un suivi de stock, une MAUVAISE valeur
    // publiée est pire qu'une ABSENCE (elle crée un faux Δ → fausse conso).
    //  - 0 capteur  → dedock
    //  - 1 à 3      → erreur capteur (dégradé) : on ne publie pas de poids
    if (valid_sensors < 4) {
        if (valid_sensors == 0) {
            Serial.println("Dedock: no sensor found, skipping publish");
        } else {
            Serial.printf("Error: %u/%u sensors valid, strict 4/4 not met, no weight\n",
                          valid_sensors, 4);
        }
        return -1.0f;
    }

    // Vérifie que le poids total est cohérent
    if (total_weight < 0.0f) {
        total_weight = 0.0f; // Ne pas retourner de poids négatif
    }
    
    // Cohérence du total : borne agrégée égale à la somme des capacités des
    // 4 pieds. (Déjà garantie par le filtre par pied ci-dessus — conservée comme
    // garde-fou lisible sur le total.)
    if (total_weight > TANK_CAPACITY_KG) {
        Serial.printf("Error: Weight %.2f kg exceeds physical maximum (%.2f kg)\n",
                      total_weight, (float)TANK_CAPACITY_KG);
        return -1.0f;
    }

    // Le zéro est celui de la calibration (facteurs Z, « tare des 4 pieds » du
    // portail) : aucune tare supplémentaire n'est soustraite ici.
    return (total_weight < 0.0f) ? 0.0f : total_weight;
}

void sensorsPowerOff() {
    // Met les HX711 en power-down (~1-2µA au lieu de ~1.4mA chacun)
    // La broche SCK est ensuite maintenue HIGH par prepareDeepSleep()
    for (uint8_t i = 0; i < 4; i++) {
        scales[i]->power_down();
    }
    Serial.println("Sensors powered down.");
}

void sensorsPowerOn() {
    // Libère le maintien (hold) posé par prepareDeepSleep AVANT de piloter SCK :
    // un pad maintenu garde son niveau figé, l'impulsion basse de power_up()
    // n'aurait alors aucun effet et l'HX711 resterait éteint. Pour les pads RTC,
    // la libération rend en plus la pad au chemin numérique (elle était pilotée
    // par le domaine RTC pendant le sommeil).
    powerReleaseSensorHold();

    // Séquence de réveil : impulsions SCK bas
    for (uint8_t i = 0; i < 4; i++) {
        scales[i]->power_up();
    }
    delay(100); // Temps de stabilisation
}

bool sensorsReadFoot(uint8_t foot_index, float& raw_out, float& kg_out,
                     uint8_t samples) {
    if (foot_index > 3) return false;
    // Comme sensorsReadTotalWeight : ne lit QUE les capteurs prêts à l'init, et
    // borne l'attente (wait_ready_timeout) pour ne jamais bloquer sur un DOUT
    // qui reste HIGH (pied absent / HX711 KO).
    if (!(g_ready_mask & (1u << foot_index))) {
        Serial.printf("Foot %u never ready at init, skipping\n", foot_index + 1);
        return false;
    }
    HX711* scale = scales[foot_index];
    float sum_raw = 0.0f;
    uint8_t valid = 0;
    for (uint8_t i = 0; i < samples; i++) {
        if (scale->wait_ready_timeout(500, 1)) {
            // read_average() renvoie la valeur BRUTE (avant offset/échelle) :
            // c'est elle qui sert à tarer (Z) et à calculer le facteur (C).
            sum_raw += (float)scale->read_average(1);
            valid++;
        }
        if (i + 1 < samples) delay(SENSOR_READ_DELAY_MS);
    }
    if (valid == 0) {
        Serial.printf("Foot %u: no valid reading\n", foot_index + 1);
        return false;
    }

    // Pas de filtre de plausibilité ici (contrairement aux lectures de mesure) :
    // on veut la valeur TELLE QUELLE, c'est le but d'un affichage de calibration.
    const float raw = sum_raw / (float)valid;
    const float c = scale->get_scale();
    raw_out = raw;
    kg_out = (c != 0.0f) ? (raw - (float)scale->get_offset()) / c : 0.0f;
    return true;
}
