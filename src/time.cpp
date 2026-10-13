#include "time.hpp"

#include <esp_sntp.h> // sntp_get_sync_status() / sntp_set_sync_mode()

// =============================================================================
// GESTION DU TEMPS (NTP + RTC)
// =============================================================================

void timeConfigureTimezone() {
    // ConfigTzTime configure le fuseau ET démarre le SNTP. Sans lui, l'heure
    // RTC s'affiche en UTC même si elle est valide.
    configTzTime("CET-1CEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00", "time.nist.gov");
}

bool timeInit(const char* ntpServer, const char* timezone, uint16_t timeout_sec) {
    // Synchro SYSTÉMATIQUE à chaque réveil, sans se fier à l'heure RTC : le RTC
    // survit au deep sleep mais DÉRIVE (RTC_SLOW_CLK = oscillateur RC interne,
    // pas de quartz 32 kHz → plusieurs % d'erreur, soit des dizaines de minutes
    // par jour). Une heure RTC « valide » est donc toujours valide et ne prouve
    // rien : c'est précisément ce qui empêchait toute correction.
    Serial.println("Configuring timezone + NTP...");
    configTzTime(timezone, ntpServer);
    // IMMED : l'heure est corrigée d'un coup à la réponse. Le mode « smooth »
    // (adjtime) n'a aucun sens sur un appareil qui dort : il étalerait la
    // correction et laisserait un statut IN_PROGRESS.
    sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);

    // ATTENTION : getLocalTime() ne prouve PAS qu'une réponse NTP est arrivée —
    // il rend vrai dès que la date est plausible, ce qui est déjà le cas grâce
    // au RTC. Seul sntp_get_sync_status() dit que le SNTP a répondu.
    // (Le statut COMPLETED est consommé à la lecture → on garde la valeur.)
    sntp_sync_status_t status = SNTP_SYNC_STATUS_RESET;
    uint32_t start = millis();
    while ((status = sntp_get_sync_status()) != SNTP_SYNC_STATUS_COMPLETED &&
           (millis() - start) < (uint32_t)timeout_sec * 1000UL) {
        delay(100);
        Serial.print(".");
    }

    if (status != SNTP_SYNC_STATUS_COMPLETED) {
        // Échec : on garde l'heure RTC (locale) — mieux qu'une heure fausse.
        Serial.println("\nNTP sync FAILED (heure RTC conservee)");
        return false;
    }

    struct tm timeinfo;
    time_t now = time(nullptr);
    localtime_r(&now, &timeinfo);
    char buffer[64];
    strftime(buffer, sizeof(buffer), "%a %b %d %Y %H:%M:%S", &timeinfo);
    Serial.print("\nNTP synced: ");
    Serial.println(buffer);
    return true;
}

time_t timeGetTimestamp() {
    time_t now;
    time(&now);
    return now;
}

bool timeIsValid() {
    time_t now = timeGetTimestamp();
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    // Vérifie si l'année est plausible (>= 2020)
    // Pas de borne haute : le device doit continuer à fonctionner après 2030
    return (timeinfo.tm_year >= 120);
}
