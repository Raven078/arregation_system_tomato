#include "irrigation_logic.h"
#include "motor_control.h"
#include "time_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"

static const char *TAG = "Irrigation";
static int threshold = CONFIG_MOISTURE_THRESHOLD_PERCENT;
static bool pump_active = false;
static bool valve_active = false;
static TickType_t pump_stop_tick = 0;

void irrigation_logic_init(void) {
    motor_control_init();
    motor_pump_stop();
    motor_valve_close();
    ESP_LOGI(TAG, "Irrigation logic ready, threshold=%d%%", threshold);
}

void irrigation_logic_set_threshold(int t) {
    if (t >= 0 && t <= 100) threshold = t;
}

void irrigation_logic_update(const sensor_data_t *data) {
    int hour = get_current_hour();

    if (!pump_active) {
        if (data->moisture_percent < threshold && hour < 11 && data->level1 == 1) {
            motor_pump_set_speed(100);
            pump_active = true;
            ESP_LOGI(TAG, "Pump started (moist=%d%%, hour=%d, level1=%d)",
                     data->moisture_percent, hour, data->level1);
        }
    } else {
        if (data->level1 == 0) {
            motor_pump_stop();
            pump_active = false;
            pump_stop_tick = xTaskGetTickCount();
            ESP_LOGI(TAG, "Pump stopped (level1 lost)");
        }
    }

    if (!pump_active && pump_stop_tick != 0 && !valve_active) {
        if ((xTaskGetTickCount() - pump_stop_tick) >= pdMS_TO_TICKS(10000)) {
            motor_valve_open();
            valve_active = true;
            ESP_LOGI(TAG, "Valve opened (10 sec after pump stop)");
        }
    }
    if (valve_active && data->level2 == 1) {
        motor_valve_close();
        valve_active = false;
        pump_stop_tick = 0;
        ESP_LOGI(TAG, "Valve closed (level2 detected)");
    }
}