#include "sensors.h"
#include "wifi_app.h"
#include "time_manager.h"
#include "irrigation_logic.h"
#include "file_logger.h"
#include "motor_control.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "sdkconfig.h"

static const char *TAG = "Main";
static char device_id[13];
static int last_level1 = -1, last_level2 = -1;
static char last_pump_state[4] = "off";
static char last_valve_state[4] = "off";
static esp_timer_handle_t minute_timer = NULL;

static float minute_moisture_sum = 0;
static float minute_temp_sum = 0;
static int minute_readings_count = 0;

// Отправка мгновенных данных (по событиям)
static void send_instant_data(void) {
    sensor_data_t data = sensors_read();
    if (!wifi_is_connected()) return;

    const char *pump_state = is_pump_running() ? "on" : "off";
    const char *valve_state = is_valve_open() ? "on" : "off";

    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    cJSON *device = cJSON_CreateObject();
    cJSON_AddStringToObject(device, "name", CONFIG_DEVICE_NAME);
    cJSON_AddStringToObject(device, "id", device_id);
    cJSON_AddStringToObject(device, "type", "ESP32-C3_Sensor_Monitor");
    cJSON_AddItemToObject(root, "device", device);

    time_t now = time(NULL);
    struct tm *timeinfo = localtime(&now);
    char time_str[9];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", timeinfo);
    cJSON_AddStringToObject(root, "time", time_str);

    cJSON *sensors = cJSON_CreateObject();
    cJSON *moisture = cJSON_CreateObject();
    cJSON_AddNumberToObject(moisture, "percent", data.moisture_percent);
    cJSON_AddNumberToObject(moisture, "raw", data.raw_adc);
    cJSON_AddNumberToObject(moisture, "voltage_mv", data.voltage_mV);
    cJSON_AddStringToObject(moisture, "unit", "%");
    cJSON_AddItemToObject(sensors, "moisture", moisture);

    cJSON *temperature = cJSON_CreateObject();
    cJSON_AddNumberToObject(temperature, "value", data.temperature);
    cJSON_AddStringToObject(temperature, "unit", "°C");
    cJSON_AddItemToObject(sensors, "temperature", temperature);

    cJSON *levels = cJSON_CreateObject();
    cJSON *s1 = cJSON_CreateObject();
    cJSON_AddBoolToObject(s1, "detected", data.level1 == 1);
    cJSON_AddStringToObject(s1, "status", data.level1 ? "detected" : "not_detected");
    cJSON_AddItemToObject(levels, "sensor_1", s1);
    cJSON *s2 = cJSON_CreateObject();
    cJSON_AddBoolToObject(s2, "detected", data.level2 == 1);
    cJSON_AddStringToObject(s2, "status", data.level2 ? "detected" : "not_detected");
    cJSON_AddItemToObject(levels, "sensor_2", s2);
    cJSON_AddItemToObject(sensors, "levels", levels);
    cJSON_AddItemToObject(root, "sensors", sensors);

    cJSON_AddStringToObject(root, "pump", pump_state);
    cJSON_AddStringToObject(root, "valve", valve_state);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json_str) {
        ESP_LOGI(TAG, "Instant JSON (event): %s", json_str);
        tcp_send_data(json_str);
        free(json_str);
    }
}

// Отправка усреднённых данных (раз в минуту, в 00 секунд)
static void send_averaged_data(void) {
    if (!wifi_is_connected()) return;
    if (minute_readings_count == 0) {
        // нет накоплений – берём текущие показания
        sensor_data_t data = sensors_read();
        minute_moisture_sum = data.moisture_percent;
        minute_temp_sum = data.temperature;
        minute_readings_count = 1;
    }

    float avg_moisture = minute_moisture_sum / minute_readings_count;
    float avg_temp = minute_temp_sum / minute_readings_count;

    sensor_data_t current_data = sensors_read();
    const char *pump_state = is_pump_running() ? "on" : "off";
    const char *valve_state = is_valve_open() ? "on" : "off";

    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    cJSON *device = cJSON_CreateObject();
    cJSON_AddStringToObject(device, "name", CONFIG_DEVICE_NAME);
    cJSON_AddStringToObject(device, "id", device_id);
    cJSON_AddStringToObject(device, "type", "ESP32-C3_Sensor_Monitor");
    cJSON_AddItemToObject(root, "device", device);

    time_t now = time(NULL);
    struct tm *timeinfo = localtime(&now);
    char time_str[9];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", timeinfo);
    cJSON_AddStringToObject(root, "time", time_str);

    cJSON *sensors = cJSON_CreateObject();
    cJSON *moisture = cJSON_CreateObject();
    cJSON_AddNumberToObject(moisture, "percent", avg_moisture);
    cJSON_AddNumberToObject(moisture, "raw", current_data.raw_adc);
    cJSON_AddNumberToObject(moisture, "voltage_mv", current_data.voltage_mV);
    cJSON_AddStringToObject(moisture, "unit", "%");
    cJSON_AddItemToObject(sensors, "moisture", moisture);

    cJSON *temperature = cJSON_CreateObject();
    cJSON_AddNumberToObject(temperature, "value", avg_temp);
    cJSON_AddStringToObject(temperature, "unit", "°C");
    cJSON_AddItemToObject(sensors, "temperature", temperature);

    cJSON *levels = cJSON_CreateObject();
    cJSON *s1 = cJSON_CreateObject();
    cJSON_AddBoolToObject(s1, "detected", current_data.level1 == 1);
    cJSON_AddStringToObject(s1, "status", current_data.level1 ? "detected" : "not_detected");
    cJSON_AddItemToObject(levels, "sensor_1", s1);
    cJSON *s2 = cJSON_CreateObject();
    cJSON_AddBoolToObject(s2, "detected", current_data.level2 == 1);
    cJSON_AddStringToObject(s2, "status", current_data.level2 ? "detected" : "not_detected");
    cJSON_AddItemToObject(levels, "sensor_2", s2);
    cJSON_AddItemToObject(sensors, "levels", levels);
    cJSON_AddItemToObject(root, "sensors", sensors);

    cJSON_AddStringToObject(root, "pump", pump_state);
    cJSON_AddStringToObject(root, "valve", valve_state);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json_str) {
        ESP_LOGI(TAG, "Minute averaged JSON: %s", json_str);
        tcp_send_data(json_str);
        free(json_str);
    }

    minute_moisture_sum = 0;
    minute_temp_sum = 0;
    minute_readings_count = 0;
}

// Колбэк минутного таймера
static void minute_timer_callback(void *arg) {
    send_averaged_data();
    // Перезапуск периодического таймера (на следующую минуту)
    esp_timer_stop(minute_timer);
    esp_timer_start_periodic(minute_timer, 60 * 1000000ULL);
}

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

    sensors_get_device_id(device_id, sizeof(device_id));
    ESP_LOGI(TAG, "Device: %s, ID: %s", CONFIG_DEVICE_NAME, device_id);

    // Ожидание подключения WiFi (до 30 сек)
    int wait = 0;
    while (!wifi_is_connected() && wait < 30) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        wait++;
    }

    if (wifi_is_connected()) {
        ESP_LOGI(TAG, "WiFi connected, syncing time...");
        if (time_sync_from_tcp()) {
            file_logger_check_new_day();
            file_logger_restart_timer();  // запускаем 10-минутный таймер
        }
    } else {
        ESP_LOGW(TAG, "WiFi not connected, time will be synced later");
    }
    time_manager_init();

    // Запуск минутного таймера, привязанного к реальному времени
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    int next_sec = 60 - tm_now.tm_sec;
    if (next_sec == 60) next_sec = 0;
    int64_t delay_us = next_sec * 1000000LL;

    const esp_timer_create_args_t minute_args = {
        .callback = minute_timer_callback,
        .name = "minute_timer"
    };
    esp_timer_create(&minute_args, &minute_timer);
    if (next_sec == 0) {
        // уже ровная минута – отправляем сразу
        send_averaged_data();
        esp_timer_start_periodic(minute_timer, 60 * 1000000ULL);
    } else {
        esp_timer_start_once(minute_timer, delay_us);
    }

    // Основной цикл: накопление и отслеживание событий
    while (1) {
        sensor_data_t data = sensors_read();
        // Накопление для минутного и 10-минутного усреднения
        minute_moisture_sum += data.moisture_percent;
        minute_temp_sum += data.temperature;
        minute_readings_count++;
        file_logger_accumulate(data.moisture_percent, data.temperature);

        int level1 = data.level1;
        int level2 = data.level2;
        const char *pump_state = is_pump_running() ? "on" : "off";
        const char *valve_state = is_valve_open() ? "on" : "off";

        // Внеплановая отправка только при изменениях
        if (level1 != last_level1 || level2 != last_level2 ||
            strcmp(pump_state, last_pump_state) != 0 ||
            strcmp(valve_state, last_valve_state) != 0) {
            send_instant_data();
            last_level1 = level1;
            last_level2 = level2;
            strcpy(last_pump_state, pump_state);
            strcpy(last_valve_state, valve_state);
        }

        // Логирование событий в файл (при изменении состояния)
        if (strcmp(pump_state, last_pump_state) != 0) {
            file_logger_log_event(pump_state, "pump");
        }
        if (strcmp(valve_state, last_valve_state) != 0) {
            file_logger_log_event(valve_state, "valve");
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}