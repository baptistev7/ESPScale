// Test hôte du comptage des sacs : g++ -I include tools/bagcount_test.cpp src/bagcount.cpp -o /tmp/bagcount_test
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "bagcount.hpp"

static uint32_t t_ms;
static float kg;
static BagCounter c;

// Un échantillon par seconde, bruit ±0,15 kg déterministe.
static void sample() {
    static int i;
    const float noise = ((i++ * 7) % 5 - 2) * 0.075f;
    bagCounterFeed(c, kg + noise, t_ms);
    t_ms += 1000;
}
static void wait(int s) { for (int i = 0; i < s; i++) sample(); }
// Un sac versé en 2 s (rampe), puis la pause.
static void pour(float bag, int pause_s) {
    kg += bag / 2; sample();
    kg += bag / 2; sample();
    wait(pause_s);
}
static void start(float w) { t_ms = 0; kg = w; bagCounterInit(c, w, 0, 15.0f); }
static int fails;
static void expect(const char* name, uint16_t got, uint16_t want) {
    printf("%-44s %s (got %u, want %u)\n", name, got == want ? "ok  " : "FAIL", got, want);
    if (got != want) fails++;
}

int main() {
    start(100); for (int i = 0; i < 4; i++) pour(15.4f, 4); wait(3);
    expect("4 sacs de 15,4 kg, pauses de 4 s", bagCounterFinish(c, kg), 4);

    start(100); for (int i = 0; i < 15; i++) pour(16.0f, 3); wait(3);
    expect("15 sacs de 16 kg (+1 kg chacun), pauses 3 s", bagCounterFinish(c, kg), 15);

    start(100); for (int i = 0; i < 15; i++) pour(16.0f, 0); wait(3);
    expect("15 sacs de 16 kg d'un seul palier (limite)", bagCounterFinish(c, kg), 16);

    start(100); for (int i = 0; i < 10; i++) pour(15.0f, 0); wait(3);
    expect("10 sacs de 15 kg enchaînés sans pause", bagCounterFinish(c, kg), 10);

    start(100); pour(8.0f, 5); pour(8.0f, 5); wait(3);
    expect("un sac versé en deux fois (8 + 8 kg)", bagCounterFinish(c, kg), 1);

    start(100); kg += 5; wait(4); kg -= 5; wait(4); pour(15.2f, 4); wait(3);
    expect("appui de 5 kg sur le couvercle puis un sac", bagCounterFinish(c, kg), 1);

    start(100); pour(7.0f, 5); wait(3);
    expect("demi-sac seul : aucun sac", bagCounterFinish(c, kg), 0);

    start(100); pour(31.0f, 4); wait(3);
    expect("deux sacs d'un coup (31 kg)", bagCounterFinish(c, kg), 2);

    expect("delta 30,9 kg (essai réel)", bagCountFromDelta(30.9f, 15.0f), 2);
    expect("delta 14,6 kg (capteur bruité)", bagCountFromDelta(14.6f, 15.0f), 1);
    expect("delta 14,4 kg", bagCountFromDelta(14.4f, 15.0f), 0);

    printf(fails ? "\n%d ÉCHEC(S)\n" : "\ntout est bon\n", fails);
    return fails ? 1 : 0;
}
