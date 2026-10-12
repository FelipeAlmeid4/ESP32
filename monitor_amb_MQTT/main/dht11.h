#pragma once

#include "driver/gpio.h"
#include "esp_err.h"

typedef struct {
    float temperature; /* graus Celsius */
    float humidity;    /* % UR */
} dht11_reading_t;

/* Configura o pino (open-drain com pull-up). Chamar uma vez. */
esp_err_t dht11_init(gpio_num_t pin);

/*
 * Faz uma leitura do sensor.
 * Retorna ESP_ERR_TIMEOUT se o sensor nao respondeu e
 * ESP_ERR_INVALID_CRC se o checksum nao bateu.
 * O DHT11 precisa de pelo menos 1 s entre leituras.
 */
esp_err_t dht11_read(gpio_num_t pin, dht11_reading_t *out);
