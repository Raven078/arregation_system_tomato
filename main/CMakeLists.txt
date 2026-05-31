idf_component_register(
    SRCS 
        main.c 
        sensors.c 
        wifi_app.c
        time_manager.c
        motor_control.c
        irrigation_logic.c
        file_logger.c      # новый
    INCLUDE_DIRS "."
    REQUIRES 
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        lwip
        esp_timer
        cjson
        onewire_bus
        ds18b20
        esp_adc
        esp_driver_gpio
        esp_driver_ledc
        spiffs           # для файловой системы
)