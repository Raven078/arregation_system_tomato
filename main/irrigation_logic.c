#include "irrigation_logic.h"
#include "motor_control.h"
#include "time_manager.h"
#include "data_sender.h"
#include "file_logger.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "Irrigation";

#define THRESHOLD           CONFIG_MOISTURE_THRESHOLD
#define START_HOUR          CONFIG_PUMP_START_HOUR
#define END_HOUR            CONFIG_PUMP_END_HOUR
#define STOP_DELAY          CONFIG_PUMP_STOP_DELAY_AFTER_NO_WATER
#define MAX_RUN_TIME        CONFIG_MAX_PUMP_RUN_TIME
#define VALVE_DELAY         CONFIG_VALVE_OPEN_DELAY_AFTER_PUMP_STOP
#define LEVEL_DEBOUNCE      CONFIG_LEVEL_DEBOUNCE_DELAY
#define DEVICE_NAME         CONFIG_DEVICE_NAME

static bool pump_active = false;
static bool valve_active = false;
static TickType_t pump_stop_tick = 0;
static TickType_t pump_start_tick = 0;
static bool no_water_detected = false;
static TickType_t no_water_tick = 0;
static int prev_level1 = -1;
static int prev_level2 = -1;
static TickType_t level_change_tick = 0;
static bool level_change_pending = false;

void irrigation_logic_set_pump_state(bool active) {
    pump_active = active;
    if (active) {
        pump_start_tick = xTaskGetTickCount();
        ESP_LOGI(TAG, "Pump state set to active (manual)");
    } else {
        pump_stop_tick = xTaskGetTickCount();
        ESP_LOGI(TAG, "Pump state set to inactive (manual)");
    }
}

void irrigation_logic_set_valve_state(bool active) {
    valve_active = active;
    if (active) {
        ESP_LOGI(TAG, "Valve state set to active (manual)");
    } else {
        ESP_LOGI(TAG, "Valve state set to inactive (manual)");
    }
}

bool irrigation_logic_is_pump_active(void) { return pump_active; }
bool irrigation_logic_is_valve_active(void) { return valve_active; }

void irrigation_logic_init(void) {
    motor_control_init();
    motor_pump_stop();
    motor_valve_close();
    ESP_LOGI(TAG, "Irrigation logic ready, threshold=%d%%, hours=%d-%d, stop_delay=%ds, max_run=%dm, valve_delay=%ds, debounce=%ds",
             THRESHOLD, START_HOUR, END_HOUR, STOP_DELAY, MAX_RUN_TIME, VALVE_DELAY, LEVEL_DEBOUNCE);
    sensor_data_t init = sensors_read();
    prev_level1 = init.level1;
    prev_level2 = init.level2;
}

void irrigation_logic_update(const sensor_data_t *data) {
    int hour = get_current_hour();
    TickType_t now_tick = xTaskGetTickCount();

    // Проверка изменения уровней с debounce
    if (data->level1 != prev_level1 || data->level2 != prev_level2) {
        if (!level_change_pending) {
            level_change_tick = now_tick;
            level_change_pending = true;
        }
        if ((now_tick - level_change_tick) >= pdMS_TO_TICKS(LEVEL_DEBOUNCE * 1000)) {
            prev_level1 = data->level1;
            prev_level2 = data->level2;
            level_change_pending = false;
            data_sender_check_events(data->level1, data->level2, pump_active, valve_active);
            ESP_LOGI(TAG, "Level changed after debounce: level1=%d, level2=%d", data->level1, data->level2);
        }
    } else {
        if (level_change_pending) {
            level_change_pending = false;
        }
    }

    // Автоматическое включение насоса
    bool auto_condition = (hour >= START_HOUR && hour < END_HOUR) &&
                          (data->level2 == 1) &&
                          (data->moisture_percent < THRESHOLD);

    if (!pump_active) {
        if (auto_condition) {
            motor_pump_start();
            pump_active = true;
            pump_start_tick = now_tick;
            ESP_LOGI(TAG, "Pump started automatically (moist=%d%%, hour=%d)", data->moisture_percent, hour);
            file_logger_log_event(DEVICE_NAME, "pump", "on");
            data_sender_check_events(data->level1, data->level2, true, valve_active);
        }
    } else {
        // Отключение при отсутствии воды
        if (data->level2 == 0) {
            if (!no_water_detected) {
                no_water_detected = true;
                no_water_tick = now_tick;
                ESP_LOGI(TAG, "No water detected, waiting %d seconds before stopping pump", STOP_DELAY);
            }
            if ((now_tick - no_water_tick) >= pdMS_TO_TICKS(STOP_DELAY * 1000)) {
                motor_pump_stop();
                pump_active = false;
                pump_stop_tick = now_tick;
                ESP_LOGI(TAG, "Pump stopped (no water for %d sec)", STOP_DELAY);
                file_logger_log_event(DEVICE_NAME, "pump", "off");
                data_sender_check_events(data->level1, data->level2, false, valve_active);
                no_water_detected = false;
            }
        } else {
            no_water_detected = false;
        }

        // Отключение по таймауту
        if ((now_tick - pump_start_tick) >= pdMS_TO_TICKS(MAX_RUN_TIME * 60 * 1000)) {
            motor_pump_stop();
            pump_active = false;
            pump_stop_tick = now_tick;
            ESP_LOGI(TAG, "Pump stopped (max run time %d minutes reached)", MAX_RUN_TIME);
            file_logger_log_event(DEVICE_NAME, "pump", "timeout");
            data_sender_check_events(data->level1, data->level2, false, valve_active);
        }
    }

    // Логика клапана
    if (!pump_active && pump_stop_tick != 0 && !valve_active) {
        if ((now_tick - pump_stop_tick) >= pdMS_TO_TICKS(VALVE_DELAY * 1000)) {
            if (data->level1 == 0) {
                motor_valve_open();
                valve_active = true;
                ESP_LOGI(TAG, "Valve opened automatically (after %d sec from pump stop)", VALVE_DELAY);
                file_logger_log_event(DEVICE_NAME, "valve", "on");
                data_sender_check_events(data->level1, data->level2, pump_active, true);
            } else {
                ESP_LOGI(TAG, "Valve not opened (upper level detected)");
            }
        }
    }

    if (valve_active && data->level1 == 1) {
        motor_valve_close();
        valve_active = false;
        pump_stop_tick = 0;
        ESP_LOGI(TAG, "Valve closed (upper level detected)");
        file_logger_log_event(DEVICE_NAME, "valve", "off");
        data_sender_check_events(data->level1, data->level2, pump_active, false);
    }

    if (pump_stop_tick != 0 && valve_active) {
        pump_stop_tick = 0;
    }
}