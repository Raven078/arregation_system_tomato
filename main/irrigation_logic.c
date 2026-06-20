#include "irrigation_logic.h"
#include "motor_control.h"
#include "time_manager.h"
#include "file_logger.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"

static const char *TAG = "Irrigation";
static int threshold = CONFIG_MOISTURE_THRESHOLD_PERCENT;
static bool pump_active = false;
static bool valve_active = false;
static TickType_t pump_stop_tick = 0;
static TickType_t pump_start_tick = 0;   // для таймаута работы насоса
static const TickType_t PUMP_MAX_RUN_TIME = pdMS_TO_TICKS(5 * 60 * 1000); // 5 минут макс. работа

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

    // ---- Логика насоса ----
    if (!pump_active) {
        // Включение насоса: влажность ниже порога, время до 11:00, есть вода (нижний уровень = 1),
        // бак не переполнен (верхний уровень = 0)
        if (data->moisture_percent < threshold && hour < 11 &&
            data->level2 == 1 && data->level1 == 0) {
            motor_pump_set_speed(100);
            pump_active = true;
            pump_start_tick = xTaskGetTickCount();
            ESP_LOGI(TAG, "Pump started (moist=%d%%, hour=%d, level2=%d, level1=%d)",
                     data->moisture_percent, hour, data->level2, data->level1);
            file_logger_log_event("on", "pump");
        }
    } else {
        // Выключение насоса при отсутствии воды (нижний уровень 0) или переполнении бака (верхний уровень 1)
        bool stop_condition = (data->level2 == 0) || (data->level1 == 1);
        TickType_t now = xTaskGetTickCount();
        bool timeout = (now - pump_start_tick) >= PUMP_MAX_RUN_TIME;

        if (stop_condition || timeout) {
            motor_pump_stop();
            pump_active = false;
            pump_stop_tick = now;
            if (stop_condition) {
                ESP_LOGI(TAG, "Pump stopped: level2=%d, level1=%d", data->level2, data->level1);
            } else {
                ESP_LOGI(TAG, "Pump stopped by timeout (max run time)");
                file_logger_log_event("timeout", "pump");
            }
        }
    }

    // ---- Логика клапана (заполнение бака) ----
    // Клапан открывается через 10 секунд после остановки насоса, если он ещё не открыт
    if (!pump_active && pump_stop_tick != 0 && !valve_active) {
        if ((xTaskGetTickCount() - pump_stop_tick) >= pdMS_TO_TICKS(10000)) {
            motor_valve_open();
            valve_active = true;
            ESP_LOGI(TAG, "Valve opened (10 sec after pump stop)");
            file_logger_log_event("off", "pump"); // насос выключен, теперь открываем клапан для заполнения
        }
    }

    // Закрытие клапана при достижении верхнего уровня (бак полон)
    if (valve_active && data->level1 == 1) {
        motor_valve_close();
        valve_active = false;
        pump_stop_tick = 0;
        ESP_LOGI(TAG, "Valve closed (upper level detected)");
        file_logger_log_event("closed", "valve");
    }
}