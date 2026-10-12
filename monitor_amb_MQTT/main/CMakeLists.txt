# ESP-IDF 6.0+: mqtt e cJSON vem do Component Registry (ver idf_component.yml)
if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    set(extra_requires espressif__mqtt espressif__cjson)
else()
    set(extra_requires mqtt json)
endif()

idf_component_register(
    SRCS "main.c" "wifi_sta.c" "dht11.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_driver_gpio esp_timer ${extra_requires}
)
