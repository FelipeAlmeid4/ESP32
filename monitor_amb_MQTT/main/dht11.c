#include "dht11.h"

#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

/* Espera enquanto o pino estiver em 'level'. Retorna a duracao em us ou -1 em timeout. */
static int wait_while(gpio_num_t pin, int level, int timeout_us)
{
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(pin) == level) {
        if (esp_timer_get_time() - start > timeout_us) {
            return -1;
        }
    }
    return (int)(esp_timer_get_time() - start);
}

esp_err_t dht11_init(gpio_num_t pin)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err == ESP_OK) {
        gpio_set_level(pin, 1); /* linha em repouso (liberada) */
    }
    return err;
}

esp_err_t dht11_read(gpio_num_t pin, dht11_reading_t *out)
{
    uint8_t data[5] = {0};
    esp_err_t err = ESP_OK;

    /* Sinal de start: mantem a linha em nivel baixo por >= 18 ms */
    gpio_set_level(pin, 0);
    vTaskDelay(pdMS_TO_TICKS(20));

    /* A partir daqui a temporizacao e critica (~5 ms): bloqueia interrupcoes */
    portENTER_CRITICAL(&s_mux);
    gpio_set_level(pin, 1);

    /* Resposta do sensor: ~20-40 us alto, 80 us baixo, 80 us alto */
    if (wait_while(pin, 1, 60) < 0 ||
        wait_while(pin, 0, 100) < 0 ||
        wait_while(pin, 1, 100) < 0) {
        err = ESP_ERR_TIMEOUT;
    }

    /* 40 bits: cada bit = 50 us baixo + (26-28 us alto => 0 | 70 us alto => 1) */
    for (int i = 0; i < 40 && err == ESP_OK; i++) {
        if (wait_while(pin, 0, 80) < 0) {
            err = ESP_ERR_TIMEOUT;
            break;
        }
        int high_us = wait_while(pin, 1, 100);
        if (high_us < 0) {
            err = ESP_ERR_TIMEOUT;
            break;
        }
        data[i / 8] <<= 1;
        if (high_us > 40) {
            data[i / 8] |= 1;
        }
    }
    portEXIT_CRITICAL(&s_mux);

    if (err != ESP_OK) {
        return err;
    }
    if ((uint8_t)(data[0] + data[1] + data[2] + data[3]) != data[4]) {
        return ESP_ERR_INVALID_CRC;
    }

    out->humidity = data[0] + data[1] * 0.1f;
    out->temperature = data[2] + (data[3] & 0x7F) * 0.1f;
    if (data[3] & 0x80) { /* bit de sinal (temperaturas negativas) */
        out->temperature = -out->temperature;
    }
    return ESP_OK;
}
