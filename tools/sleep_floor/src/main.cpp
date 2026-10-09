// Recherche du plancher de veille T5 V2.4 : une configuration par réveil.
// Sommeil de 20 s par étape ; l'étape repart de 0 à chaque démarrage à froid.
//   0  minimal : écran coupé (GPIO 12 bas, tenu côté RTC), timer seul
//   1  + domaines optionnels forcés OFF (RTC_PERIPH, RTC_FAST_MEM, XTAL, RTC8M, VDD_SDIO)
//   2  + broches libres en entrée avec pull-down
//   3  + broches libres en entrée SANS pull (remplace 2)
//   4  + GPIO 19 (LED) tenue basse
//   5  + UART (1, 3) en entrée sans pull
//   6  + isolation des GPIO par l'IDF (esp_sleep_config_gpio_isolate)
#include <Arduino.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>

RTC_DATA_ATTR int step = 0;

// Broches sans rôle pendant ce test (ni 0 = BOOT tiré haut par la carte, ni
// 12 = coupure écran, ni 34-39 entrées seules sans pull).
static const gpio_num_t kFree[] = {
    GPIO_NUM_2, GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_13, GPIO_NUM_14, GPIO_NUM_15,
    GPIO_NUM_16, GPIO_NUM_17, GPIO_NUM_18, GPIO_NUM_21, GPIO_NUM_22,
    GPIO_NUM_23, GPIO_NUM_25, GPIO_NUM_26, GPIO_NUM_27, GPIO_NUM_32, GPIO_NUM_33,
};

static void freePins(gpio_pull_mode_t pull) {
    for (gpio_num_t p : kFree) {
        gpio_reset_pin(p);
        gpio_set_direction(p, GPIO_MODE_INPUT);
        gpio_set_pull_mode(p, pull);
        if (rtc_gpio_is_valid_gpio(p)) {   // le pull d'une pad RTC en sommeil
            rtc_gpio_init(p);              // est celui du domaine RTC
            rtc_gpio_set_direction(p, RTC_GPIO_MODE_INPUT_ONLY);
            if (pull == GPIO_PULLDOWN_ONLY) { rtc_gpio_pullup_dis(p); rtc_gpio_pulldown_en(p); }
            else                            { rtc_gpio_pullup_dis(p); rtc_gpio_pulldown_dis(p); }
        }
    }
}

void setup() {
    Serial.begin(115200);
    const int s = step;

    rtc_gpio_init(GPIO_NUM_12);
    rtc_gpio_set_direction(GPIO_NUM_12, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_set_level(GPIO_NUM_12, 0);
    rtc_gpio_hold_en(GPIO_NUM_12);

    if (s >= 1) {
        esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
        esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);
        esp_sleep_pd_config(ESP_PD_DOMAIN_XTAL, ESP_PD_OPTION_OFF);
        esp_sleep_pd_config(ESP_PD_DOMAIN_RTC8M, ESP_PD_OPTION_OFF);
        esp_sleep_pd_config(ESP_PD_DOMAIN_VDDSDIO, ESP_PD_OPTION_OFF);
    }
    if (s == 2) freePins(GPIO_PULLDOWN_ONLY);
    if (s >= 3) freePins(GPIO_FLOATING);
    if (s >= 4) {
        gpio_reset_pin(GPIO_NUM_19);
        gpio_set_direction(GPIO_NUM_19, GPIO_MODE_OUTPUT);
        gpio_set_level(GPIO_NUM_19, 0);
        gpio_hold_en(GPIO_NUM_19);
    }
    if (s >= 5) {
        Serial.printf("floor: etape %d\n", s);
        Serial.flush();
        Serial.end();
        for (gpio_num_t p : {GPIO_NUM_1, GPIO_NUM_3}) {
            gpio_reset_pin(p);
            gpio_set_direction(p, GPIO_MODE_INPUT);
            gpio_set_pull_mode(p, GPIO_FLOATING);
        }
    } else {
        Serial.printf("floor: etape %d\n", s);
        Serial.flush();
    }
    if (s >= 6) esp_sleep_config_gpio_isolate();
    gpio_deep_sleep_hold_en();

    step = (s >= 6) ? 0 : s + 1;
    esp_sleep_enable_timer_wakeup(20ULL * 1000000ULL);
    esp_deep_sleep_start();
}
void loop() {}
