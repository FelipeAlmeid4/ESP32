idf_component_register(
    SRCS "main.c" "wifi_sta.c" "dht11.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash mqtt esp_driver_gpio esp_timer json
)
