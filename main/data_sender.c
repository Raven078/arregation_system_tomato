#include "data_sender.h"
#include "sensors.h"
#include "wifi_app.h"
#include "motor_control.h"
#include "file_logger.h"
#include "esp_log.h"
#include "cJSON.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "sdkconfig.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

static const char *TAG = "DataSender";
static char device_id[13];
static int last_level1 = -1, last_level2 = -1;
static char last_pump_state[4] = "off";
static char last_valve_state[4] = "off";
static float minute_moisture_sum = 0;
static float minute_temp_sum = 0;
static int minute_readings_count = 0;
static esp_timer_handle_t minute_timer = NULL;
static esp_timer_handle_t file_timer = NULL;
static float last_avg_moisture = 0;
static float last_avg_temp = 0;
static bool has_avg_data = false;
static bool data_requested = false;

static void send_json(const char *json_str) {
    if (json_str && wifi_is_connected()) {
        tcp_send_data(json_str);
        free((void*)json_str);
    }
}

static char* create_base_json(float moisture_val, float temp_val, int raw_adc,
                              int voltage_mv, int level1, int level2,
                              const char *pump_state, const char *valve_state,
                              int pump_speed) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

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

    cJSON *moisture_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(moisture_obj, "percent", moisture_val);
    cJSON_AddNumberToObject(moisture_obj, "raw", raw_adc);
    cJSON_AddNumberToObject(moisture_obj, "voltage_mv", voltage_mv);
    cJSON_AddStringToObject(moisture_obj, "unit", "%");
    cJSON_AddItemToObject(sensors, "moisture", moisture_obj);

    cJSON *temperature = cJSON_CreateObject();
    cJSON_AddNumberToObject(temperature, "value", temp_val);
    cJSON_AddStringToObject(temperature, "unit", "°C");
    cJSON_AddItemToObject(sensors, "temperature", temperature);

    cJSON *levels = cJSON_CreateObject();
    cJSON *s1 = cJSON_CreateObject();
    cJSON_AddBoolToObject(s1, "detected", level1 == 1);
    cJSON_AddStringToObject(s1, "status", level1 ? "detected" : "not_detected");
    cJSON_AddItemToObject(levels, "sensor_1", s1);

    cJSON *s2 = cJSON_CreateObject();
    cJSON_AddBoolToObject(s2, "detected", level2 == 1);
    cJSON_AddStringToObject(s2, "status", level2 ? "detected" : "not_detected");
    cJSON_AddItemToObject(levels, "sensor_2", s2);

    cJSON_AddItemToObject(sensors, "levels", levels);
    cJSON_AddItemToObject(root, "sensors", sensors);

    cJSON_AddStringToObject(root, "pump", pump_state);
    cJSON_AddStringToObject(root, "valve", valve_state);
    cJSON_AddNumberToObject(root, "pump_speed", pump_speed);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

static void save_to_log(float moisture, float temp, int level1, int level2,
                        bool pump_state, bool valve_state) {
    file_logger_append_data(CONFIG_DEVICE_NAME, temp, (int)moisture, pump_state, valve_state);
    ESP_LOGD(TAG, "Data saved to log file");
}

static void send_current_data(void) {
    if (!wifi_is_connected()) return;
    sensor_data_t current = sensors_read();
    const char *pump_state = is_pump_running() ? "on" : "off";
    const char *valve_state = is_valve_open() ? "on" : "off";
    int speed = motor_pump_get_speed();
    char *json = create_base_json(current.moisture_percent, current.temperature,
                                   current.raw_adc, current.voltage_mV,
                                   current.level1, current.level2,
                                   pump_state, valve_state, speed);
    if (json) {
        ESP_LOGI(TAG, "Send current data: %s", json);
        send_json(json);
        save_to_log(current.moisture_percent, current.temperature,
                    current.level1, current.level2,
                    is_pump_running(), is_valve_open());
    }
}

static void send_averaged_data(void) {
    if (!data_requested) {
        ESP_LOGD(TAG, "Data sending disabled, skipping minute send");
        return;
    }
    if (!wifi_is_connected()) return;

    if (minute_readings_count == 0) {
        sensor_data_t data = sensors_read();
        minute_moisture_sum = data.moisture_percent;
        minute_temp_sum = data.temperature;
        minute_readings_count = 1;
    }

    float avg_moisture = minute_moisture_sum / minute_readings_count;
    float avg_temp = minute_temp_sum / minute_readings_count;

    sensor_data_t current = sensors_read();
    const char *pump_state = is_pump_running() ? "on" : "off";
    const char *valve_state = is_valve_open() ? "on" : "off";
    int speed = motor_pump_get_speed();

    char *json = create_base_json(avg_moisture, avg_temp,
                                   current.raw_adc, current.voltage_mV,
                                   current.level1, current.level2,
                                   pump_state, valve_state, speed);
    if (json) {
        ESP_LOGI(TAG, "Minute averaged JSON: %s", json);
        send_json(json);
        save_to_log(avg_moisture, avg_temp,
                    current.level1, current.level2,
                    is_pump_running(), is_valve_open());
    }

    minute_moisture_sum = 0;
    minute_temp_sum = 0;
    minute_readings_count = 0;
}

static void minute_timer_callback(void *arg) {
    if (minute_readings_count > 0) {
        last_avg_moisture = minute_moisture_sum / minute_readings_count;
        last_avg_temp = minute_temp_sum / minute_readings_count;
        has_avg_data = true;
    } else {
        sensor_data_t data = sensors_read();
        last_avg_moisture = data.moisture_percent;
        last_avg_temp = data.temperature;
        has_avg_data = true;
    }

    minute_moisture_sum = 0;
    minute_temp_sum = 0;
    minute_readings_count = 0;

    if (data_requested && has_avg_data) {
        send_averaged_data();
    }

    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    int next_sec = 60 - tm.tm_sec;
    if (next_sec == 60) next_sec = 0;
    int64_t delay_us = next_sec * 1000000LL;
    esp_timer_stop(minute_timer);
    esp_timer_start_once(minute_timer, delay_us);
}

void data_sender_send_log_file(void) {
    if (!wifi_is_connected()) {
        ESP_LOGW(TAG, "WiFi not connected, cannot send log file");
        return;
    }

    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    char filename[64];
    snprintf(filename, sizeof(filename), "/spiffs/%s_%d_%d_%d.txt",
             CONFIG_DEVICE_NAME, tm_info.tm_mday, tm_info.tm_mon + 1, tm_info.tm_year + 1900);

    FILE *f = fopen(filename, "r");
    if (!f) {
        ESP_LOGW(TAG, "Log file not found: %s", filename);
        return;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_size == 0) {
        fclose(f);
        ESP_LOGW(TAG, "Log file is empty: %s", filename);
        return;
    }

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Socket creation failed for file send");
        fclose(f);
        return;
    }

    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(8888);
    inet_pton(AF_INET, "192.168.4.1", &dest_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) != 0) {
        ESP_LOGE(TAG, "Connect to server failed for file send");
        close(sock);
        fclose(f);
        return;
    }

    char header[128];
    snprintf(header, sizeof(header), "FILE:%s:%d_%d_%d:%ld\n",
             CONFIG_DEVICE_NAME, tm_info.tm_mday, tm_info.tm_mon + 1, tm_info.tm_year + 1900, file_size);
    send(sock, header, strlen(header), 0);
    ESP_LOGI(TAG, "File header sent: %s", header);

    char buffer[1024];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        send(sock, buffer, bytes_read, 0);
    }
    fclose(f);
    close(sock);
    ESP_LOGI(TAG, "Log file sent: %s (%ld bytes)", filename, file_size);
}

static void file_timer_callback(void *arg) {
    data_sender_send_log_file();
}

void data_sender_check_and_create_log(void) {
    if (!wifi_is_connected()) {
        ESP_LOGW(TAG, "WiFi not connected, cannot create log");
        return;
    }
    if (file_logger_is_today_file_exists(CONFIG_DEVICE_NAME)) {
        ESP_LOGI(TAG, "Log file for today already exists");
        data_sender_send_log_file();
        return;
    }
    sensor_data_t current = sensors_read();
    file_logger_append_data(CONFIG_DEVICE_NAME,
                            current.temperature,
                            current.moisture_percent,
                            is_pump_running(),
                            is_valve_open());
    ESP_LOGI(TAG, "Log file created with current data");
    data_sender_send_log_file();
}

static void single_file_timer_cb(void *arg) {
    data_sender_send_log_file();
    esp_timer_start_periodic(file_timer, 10 * 60 * 1000000LL);
    ESP_LOGI(TAG, "File timer started periodic (10 min)");
}

void data_sender_init(void) {
    sensors_get_device_id(device_id, sizeof(device_id));

    const esp_timer_create_args_t minute_args = {
        .callback = minute_timer_callback,
        .name = "minute_timer"
    };
    esp_timer_create(&minute_args, &minute_timer);
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    int next_sec = 60 - tm.tm_sec;
    if (next_sec == 60) next_sec = 0;
    esp_timer_start_once(minute_timer, next_sec * 1000000LL);

    const esp_timer_create_args_t file_args = {
        .callback = file_timer_callback,
        .name = "file_timer"
    };
    esp_timer_create(&file_args, &file_timer);

    struct tm tm_file;
    localtime_r(&now, &tm_file);
    int current_minute = tm_file.tm_min;
    int current_second = tm_file.tm_sec;
    int next_minute = ((current_minute / 10) + 1) * 10;
    if (next_minute >= 60) next_minute = 0;
    int delay_minutes = next_minute - current_minute;
    if (delay_minutes < 0) delay_minutes += 60;
    int delay_seconds = delay_minutes * 60 - current_second;
    if (delay_seconds < 0) delay_seconds += 60;

    static esp_timer_handle_t single_timer = NULL;
    if (single_timer) {
        esp_timer_stop(single_timer);
        esp_timer_delete(single_timer);
    }
    const esp_timer_create_args_t single_args = {
        .callback = single_file_timer_cb,
        .name = "single_file_timer"
    };
    esp_timer_create(&single_args, &single_timer);
    esp_timer_start_once(single_timer, (uint64_t)delay_seconds * 1000000LL);
    ESP_LOGI(TAG, "File timer started, first send in %d seconds (at %02d:%02d:00)",
             delay_seconds, next_minute / 60, next_minute % 60);
}

void data_sender_accumulate(float moisture, float temp) {
    minute_moisture_sum += moisture;
    minute_temp_sum += temp;
    minute_readings_count++;
}

void data_sender_check_events(int level1, int level2, bool pump_running, bool valve_open) {
    if (!data_requested) {
        ESP_LOGD(TAG, "Events not sent because data stream inactive");
        return;
    }

    const char *pump_state = pump_running ? "on" : "off";
    const char *valve_state = valve_open ? "on" : "off";
    bool changed = false;

    if (level1 != last_level1 || level2 != last_level2) changed = true;
    if (strcmp(pump_state, last_pump_state) != 0) changed = true;
    if (strcmp(valve_state, last_valve_state) != 0) changed = true;

    if (changed) {
        sensor_data_t current = sensors_read();
        int speed = motor_pump_get_speed();
        char *json = create_base_json(current.moisture_percent, current.temperature,
                                       current.raw_adc, current.voltage_mV,
                                       level1, level2, pump_state, valve_state, speed);
        if (json) {
            ESP_LOGI(TAG, "Instant JSON (event): %s", json);
            send_json(json);
            save_to_log(current.moisture_percent, current.temperature,
                        level1, level2, pump_running, valve_open);
        }

        if (strcmp(pump_state, last_pump_state) != 0) {
            file_logger_log_event(CONFIG_DEVICE_NAME, "pump", pump_state);
        }
        if (strcmp(valve_state, last_valve_state) != 0) {
            file_logger_log_event(CONFIG_DEVICE_NAME, "valve", valve_state);
        }
    }

    last_level1 = level1;
    last_level2 = level2;
    strcpy(last_pump_state, pump_state);
    strcpy(last_valve_state, valve_state);
}

void data_sender_request_data(void) {
    if (!data_requested) {
        data_requested = true;
        ESP_LOGI(TAG, "Data sending enabled");
    }
    send_current_data();
}

void data_sender_stop_data(void) {
    data_requested = false;
    ESP_LOGI(TAG, "Data sending disabled");
}

void data_sender_send_registration(void) {
    send_current_data();
}