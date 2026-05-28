#include "sensors.h"
#include "wifi_app.h"
#include "time_manager.h"
#include "irrigation_logic.h"
#include "log_manager.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "Main";
static char device_id[13];

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
    time_manager_init();
    log_manager_init();
    log_manager_start();

    sensors_get_device_id(device_id, sizeof(device_id));
    const char *dev_name = CONFIG_DEVICE_NAME;
    ESP_LOGI(TAG, "Device: %s, ID: %s", dev_name, device_id);

    int interval = CONFIG_SENSOR_READ_INTERVAL_SEC;
    while (1) {
        sensor_data_t data = sensors_read();

        printf("\n====== %s ======\n", dev_name);
        printf("Moisture: %d%% (%d mV)\n", data.moisture_percent, data.voltage_mV);
        printf("Temperature: %.2f C\n", data.temperature);
        printf("Level1: %d, Level2: %d\n", data.level1, data.level2);
        printf("ID: %s\n", device_id);

        irrigation_logic_update(&data);

        if (wifi_is_connected()) {
            char *json = sensors_create_json(&data, dev_name, device_id);
            if (json) {
                if (tcp_send_data(json) == 0) {
                    ESP_LOGI(TAG, "Data sent");
                }
                free(json);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(interval * 1000));
    }
}