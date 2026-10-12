/*
 * Monitor ambiental com DHT11 + MQTT (ESP-IDF)
 * Placa: Franzininho WiFi LAB01 (ESP32-S2)
 *
 * Topicos (prefixo configuravel em menuconfig):
 *   <prefixo>/temperatura  -> "25.0"            (a cada leitura)
 *   <prefixo>/umidade      -> "60.0"            (a cada leitura)
 *   <prefixo>/alarme       -> JSON, retido      (quando o estado do alarme muda)
 *   <prefixo>/status       -> "online"/"offline" (LWT)
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "nvs_flash.h"

#include "dht11.h"
#include "wifi_sta.h"

#define TOPIC_TEMP   CONFIG_MQTT_TOPIC_BASE "/temperatura"
#define TOPIC_HUM    CONFIG_MQTT_TOPIC_BASE "/umidade"
#define TOPIC_ALARM  CONFIG_MQTT_TOPIC_BASE "/alarme"
#define TOPIC_STATUS CONFIG_MQTT_TOPIC_BASE "/status"

#define MQTT_CONNECTED_BIT BIT0

static const char *TAG = "monitor";

static esp_mqtt_client_handle_t s_client;
static EventGroupHandle_t s_mqtt_events;

/* ------------------------------------------------------------------ */
/* LED de alarme (lado assinante)                                      */
/* ------------------------------------------------------------------ */
#if CONFIG_APP_HAS_ALARM_LED

#if CONFIG_ALARM_LED_ACTIVE_LOW
#define LED_ON_LEVEL 0
#else
#define LED_ON_LEVEL 1
#endif

static void alarm_led_set(bool on)
{
    gpio_set_level(CONFIG_ALARM_LED_GPIO, on ? LED_ON_LEVEL : !LED_ON_LEVEL);
}

static void alarm_led_init(void)
{
    gpio_reset_pin(CONFIG_ALARM_LED_GPIO);
    gpio_set_direction(CONFIG_ALARM_LED_GPIO, GPIO_MODE_OUTPUT);
    alarm_led_set(false);
}

/* Aceita o JSON publicado pelo sensor ({"estado":"ON",...}) ou texto puro
 * "ON"/"OFF"/"1"/"0", para facilitar testes manuais pelo MQTT Explorer. */
static void handle_alarm_message(const char *payload, int len)
{
    bool on = false;
    cJSON *root = cJSON_ParseWithLength(payload, len);
    const cJSON *estado = root ? cJSON_GetObjectItem(root, "estado") : NULL;

    if (cJSON_IsString(estado)) {
        on = strcasecmp(estado->valuestring, "ON") == 0;
    } else {
        on = (len == 2 && strncasecmp(payload, "ON", 2) == 0) || (len == 1 && payload[0] == '1');
    }
    cJSON_Delete(root);

    alarm_led_set(on);
    ESP_LOGW(TAG, "Alarme recebido: %.*s -> LED %s", len, payload, on ? "ACESO" : "apagado");
}

#endif /* CONFIG_APP_HAS_ALARM_LED */

/* ------------------------------------------------------------------ */
/* MQTT                                                                */
/* ------------------------------------------------------------------ */
static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT conectado a %s", CONFIG_MQTT_BROKER_URI);
        esp_mqtt_client_publish(event->client, TOPIC_STATUS, "online", 0, 1, 1);
#if CONFIG_APP_HAS_ALARM_LED
        esp_mqtt_client_subscribe(event->client, TOPIC_ALARM, 1);
        ESP_LOGI(TAG, "Assinando %s", TOPIC_ALARM);
#endif
        xEventGroupSetBits(s_mqtt_events, MQTT_CONNECTED_BIT);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT desconectado");
        xEventGroupClearBits(s_mqtt_events, MQTT_CONNECTED_BIT);
        break;

    case MQTT_EVENT_DATA:
#if CONFIG_APP_HAS_ALARM_LED
        if (event->topic_len == strlen(TOPIC_ALARM) &&
            strncmp(event->topic, TOPIC_ALARM, event->topic_len) == 0) {
            handle_alarm_message(event->data, event->data_len);
        }
#endif
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "Erro MQTT");
        break;

    default:
        break;
    }
}

static void mqtt_start(void)
{
    /* Client ID unico por placa (duas placas no mesmo broker nao se derrubam) */
    static char client_id[32];
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(client_id, sizeof(client_id), "lab01-%02x%02x%02x", mac[3], mac[4], mac[5]);

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = CONFIG_MQTT_BROKER_URI,
        .credentials.client_id = client_id,
        .session.last_will = {
            .topic = TOPIC_STATUS,
            .msg = "offline",
            .qos = 1,
            .retain = 1,
        },
    };
    if (strlen(CONFIG_MQTT_USERNAME) > 0) {
        cfg.credentials.username = CONFIG_MQTT_USERNAME;
        cfg.credentials.authentication.password = CONFIG_MQTT_PASSWORD;
    }

    s_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);
    ESP_LOGI(TAG, "Client ID: %s", client_id);
}

/* ------------------------------------------------------------------ */
/* Sensor + logica de alarme (lado publicador)                         */
/* ------------------------------------------------------------------ */
#if CONFIG_APP_HAS_SENSOR

static bool mqtt_is_connected(void)
{
    return xEventGroupGetBits(s_mqtt_events) & MQTT_CONNECTED_BIT;
}

/* Com histerese: liga acima do limite, so desliga abaixo de (limite - histerese) */
static bool over_limit(float value, int limit, bool currently_on)
{
    return currently_on ? value > (limit - CONFIG_ALARM_HYSTERESIS) : value > limit;
}

static bool publish_alarm(bool temp_alarm, bool hum_alarm, const dht11_reading_t *r)
{
    const char *motivo = temp_alarm && hum_alarm ? "temperatura+umidade"
                       : temp_alarm              ? "temperatura"
                       : hum_alarm               ? "umidade"
                                                 : "normal";
    char payload[192];
    snprintf(payload, sizeof(payload),
             "{\"estado\":\"%s\",\"motivo\":\"%s\",\"temperatura\":%.1f,\"umidade\":%.1f,"
             "\"limite_temperatura\":%d,\"limite_umidade\":%d}",
             (temp_alarm || hum_alarm) ? "ON" : "OFF", motivo, r->temperature, r->humidity,
             CONFIG_TEMP_LIMIT_C, CONFIG_HUM_LIMIT_PCT);

    /* QoS 1 + retain: quem assinar depois recebe o estado atual do alarme */
    int msg_id = esp_mqtt_client_publish(s_client, TOPIC_ALARM, payload, 0, 1, 1);
    ESP_LOGW(TAG, "Alarme publicado: %s", payload);
    return msg_id >= 0;
}

static void sensor_task(void *arg)
{
    const gpio_num_t pin = CONFIG_DHT11_GPIO;
    bool temp_alarm = false;
    bool hum_alarm = false;
    bool alarm_pending = true; /* publica o estado inicial na primeira leitura */

    ESP_ERROR_CHECK(dht11_init(pin));
    vTaskDelay(pdMS_TO_TICKS(1500)); /* DHT11 precisa de ~1 s apos energizar */

    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        dht11_reading_t r;
        esp_err_t err = ESP_FAIL;
        for (int attempt = 0; attempt < 3 && err != ESP_OK; attempt++) {
            if (attempt > 0) {
                vTaskDelay(pdMS_TO_TICKS(2000));
            }
            err = dht11_read(pin, &r);
        }

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Falha ao ler DHT11: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "Temperatura: %.1f C | Umidade: %.1f %%", r.temperature, r.humidity);

            bool new_temp = over_limit(r.temperature, CONFIG_TEMP_LIMIT_C, temp_alarm);
            bool new_hum = over_limit(r.humidity, CONFIG_HUM_LIMIT_PCT, hum_alarm);
            if (new_temp != temp_alarm || new_hum != hum_alarm) {
                temp_alarm = new_temp;
                hum_alarm = new_hum;
                alarm_pending = true;
            }

            if (mqtt_is_connected()) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%.1f", r.temperature);
                esp_mqtt_client_publish(s_client, TOPIC_TEMP, buf, 0, 0, 0);
                snprintf(buf, sizeof(buf), "%.1f", r.humidity);
                esp_mqtt_client_publish(s_client, TOPIC_HUM, buf, 0, 0, 0);

                if (alarm_pending && publish_alarm(temp_alarm, hum_alarm, &r)) {
                    alarm_pending = false;
                }
            } else {
                ESP_LOGW(TAG, "MQTT offline, leitura nao publicada");
            }
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CONFIG_SENSOR_PERIOD_S * 1000));
    }
}

#endif /* CONFIG_APP_HAS_SENSOR */

/* ------------------------------------------------------------------ */
void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    s_mqtt_events = xEventGroupCreate();

#if CONFIG_APP_HAS_ALARM_LED
    alarm_led_init();
#endif

    wifi_sta_connect(CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD);
    mqtt_start();

#if CONFIG_APP_HAS_SENSOR
    ESP_LOGI(TAG, "Limites: temperatura > %d C, umidade > %d %% (leitura a cada %d s)",
             CONFIG_TEMP_LIMIT_C, CONFIG_HUM_LIMIT_PCT, CONFIG_SENSOR_PERIOD_S);
    xTaskCreate(sensor_task, "sensor", 4096, NULL, 5, NULL);
#endif
}
