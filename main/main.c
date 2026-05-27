// main/main.c
#include "sensors.h"
#include "wifi_app.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "Main";
static char device_id[13] = {0};

void app_main(void)
{
    #ifdef CONFIG_SEND_INTERVAL_SEC
        int send_interval = CONFIG_SEND_INTERVAL_SEC;
    #else
        int send_interval = 10;
    #endif

    #ifdef CONFIG_DEVICE_NAME
        const char *device_name = CONFIG_DEVICE_NAME;
    #else
        const char *device_name = "Теплица помидорник";
    #endif

    sensors_get_device_id(device_id, sizeof(device_id));

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Устройство: %s", device_name);
    ESP_LOGI(TAG, "ID устройства: %s", device_id);
    ESP_LOGI(TAG, "Платформа: ESP32-C3 Super Mini");
    ESP_LOGI(TAG, "ESP-IDF версия: 6.1");
    ESP_LOGI(TAG, "========================================");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    sensors_init();
    wifi_init_sta();

    ESP_LOGI(TAG, "Начало мониторинга (интервал: %d сек)", send_interval);

    while (1) {
        sensor_data_t sensor_data = sensors_read();

        printf("\n==================== %s ====================\n", device_name);
        printf("[HW-390] Влажность: %d%% (RAW: %d, %d мВ)\n",
               sensor_data.moisture_percent,
               sensor_data.raw_adc,
               sensor_data.voltage_mV);
        
        if (sensor_data.temperature > -127.0f) {
            printf("[DS18B20] Температура: %.2f °C\n", sensor_data.temperature);
        } else {
            printf("[DS18B20] Температура: Н/Д\n");
        }
        
        printf("[XKC-Y25-V #1] Уровень: %s\n",
               sensor_data.level1 ? "ОБНАРУЖЕН" : "отсутствует");
        printf("[XKC-Y25-V #2] Уровень: %s\n",
               sensor_data.level2 ? "ОБНАРУЖЕН" : "отсутствует");
        printf("ID: %s\n", device_id);
        printf("========================================================\n");

        if (wifi_is_connected()) {
            char *json_str = sensors_create_json(&sensor_data, device_name, device_id);
            if (json_str != NULL) {
                if (tcp_send_data(json_str) == 0) {
                    ESP_LOGI(TAG, "Данные отправлены на сервер");
                } else {
                    ESP_LOGW(TAG, "Ошибка отправки данных");
                }
                free(json_str);
            }
        } else {
            ESP_LOGI(TAG, "[WiFi] Нет подключения");
        }

        vTaskDelay(pdMS_TO_TICKS(send_interval * 1000));
    }
}