#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "sensors.h"
#include "wifi_app.h"
#include "time_manager.h"
#include "irrigation_logic.h"
#include "file_logger.h"
#include "motor_control.h"
#include "command_server.h"
#include "data_sender.h"
#include "sdkconfig.h"

static const char *TAG = "Main";

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    sensors_init();
    wifi_init_sta();
    irrigation_logic_init();
    file_logger_init();

    ESP_LOGI(TAG, "Device: %s", CONFIG_DEVICE_NAME);

    int wait = 0;
    while (!wifi_is_connected() && wait < 30) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        wait++;
    }

    if (wifi_is_connected()) {
        ESP_LOGI(TAG, "WiFi connected, syncing time...");
        if (time_sync_from_tcp()) {
            file_logger_check_new_day();
            file_logger_restart_timer();
        }
    } else {
        ESP_LOGW(TAG, "WiFi not connected, time will be synced later");
    }
    time_manager_init();

    command_server_start();
    data_sender_init();   // data_requested = false

    // НЕ отправляем данные для регистрации – сервер получит их только по команде send_data
    // data_sender_send_registration();  // удалено

    while (1) {
        sensor_data_t data = sensors_read();
        data_sender_accumulate(data.moisture_percent, data.temperature);
        file_logger_accumulate(data.moisture_percent, data.temperature);

        bool pump_running = is_pump_running();
        bool valve_open = is_valve_open();

        data_sender_check_events(data.level1, data.level2, pump_running, valve_open);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}