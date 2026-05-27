idf_component_register(
    SRCS "sensors.c" "wifi_app.c" "main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_adc onewire_bus ds18b20 nvs_flash esp_wifi esp_netif esp_event lwip cjson
)