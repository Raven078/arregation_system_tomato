#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "time.h"
#include "command_server.h"
#include "wifi_app.h"
#include "sensors.h"
#include "motor_control.h"
#include "irrigation_logic.h"
#include "file_logger.h"
#include "time_manager.h"
#include "data_sender.h"
#include "button.h"

static const char *TAG = "Main";

void app_main(void) {
    ESP_LOGI(TAG, "Starting application");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    sensors_init();
    motor_control_init();
    wifi_init_sta();
    file_logger_init();
    time_manager_init();
    irrigation_logic_init();
    button_init();
    data_sender_init();   // инициализация отправки данных и файлового таймера

    // Убираем вызов data_sender_send_log_file() здесь — он будет вызван из wifi_app.c после получения времени

    command_server_start();

    ESP_LOGI(TAG, "System ready, waiting for commands...");

    while (1) {
        sensor_data_t data = sensors_read();
        irrigation_logic_update(&data);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}