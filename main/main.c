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
#include "log_stream.h"
#include "ota_recovery.h"
#include "sdkconfig.h"

static const char *TAG = "Main";

void app_main(void) {
    ESP_LOGI(TAG, "Starting application");

    // Инициализация NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Инициализация модуля стриминга логов
    log_stream_init();

    // Проверяем флаг OTA recovery
    if (ota_recovery_should_enter()) {
        ESP_LOGI(TAG, "Entering OTA recovery mode");
        // Запускаем сервер OTA recovery на порту из Kconfig
        ota_recovery_start_server(CONFIG_OTA_RECOVERY_PORT);
        // Остальную инициализацию пропускаем, ждём прошивку
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        return;
    }

    // Инициализация датчиков
    sensors_init();

    // Инициализация мотора и клапана
    motor_control_init();

    // Инициализация Wi‑Fi (подключение к AP)
    wifi_init_sta();

    // Инициализация файлового логгера (ожидает синхронизации времени)
    file_logger_init();

    // Инициализация менеджера времени (SNTP)
    time_manager_init();

    // Инициализация логики орошения
    irrigation_logic_init();

    // Инициализация кнопки перезагрузки (с поддержкой длинного нажатия)
    button_init();

    // Инициализация отправки данных (периодическая)
    data_sender_init();

    // Запуск сервера команд (порт 8889)
    command_server_start();

    ESP_LOGI(TAG, "System ready, waiting for commands...");

    // Основной цикл
    while (1) {
        sensor_data_t data = sensors_read();
        irrigation_logic_update(&data);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}