#include "file_logger.h"
#include "sensors.h"
#include "wifi_app.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <time.h>
#include <string.h>
#include <unistd.h>

static const char *TAG = "FileLogger";
static char current_filename[64];  // увеличен для более длинного имени
static float moisture_sum = 0;
static float temp_sum = 0;
static int readings_count = 0;
static QueueHandle_t logger_queue = NULL;
static TaskHandle_t logger_task_handle = NULL;
static esp_timer_handle_t log_timer = NULL;
static bool time_synced_flag = false;

typedef enum {
    LOGGER_CMD_ADD_RECORD,
    LOGGER_CMD_RESTART
} logger_cmd_t;

static void get_filename_for_date(struct tm *date, char *buf, size_t bufsize) {
    snprintf(buf, bufsize, "/spiffs/pomodoro_%02d_%02d_%04d.txt",
             date->tm_mday, date->tm_mon + 1, date->tm_year + 1900);
}

static void update_filename(void) {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    get_filename_for_date(tm, current_filename, sizeof(current_filename));
}

static void send_file(const char *filename) {
    if (!wifi_is_connected()) {
        ESP_LOGW(TAG, "WiFi not connected, cannot send file");
        return;
    }
    FILE *f = fopen(filename, "r");
    if (!f) {
        ESP_LOGW(TAG, "File %s not found", filename);
        return;
    }
    char line[256];
    int lines = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;
        if (tcp_send_data(line) == 0) {
            lines++;
        } else {
            ESP_LOGE(TAG, "Failed to send line");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    fclose(f);
    ESP_LOGI(TAG, "Sent %d lines from %s", lines, filename);
}

static void delete_file(const char *filename) {
    if (unlink(filename) == 0) {
        ESP_LOGI(TAG, "Deleted %s", filename);
    } else {
        ESP_LOGE(TAG, "Failed to delete %s", filename);
    }
}

static void print_file_content(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s for reading", filename);
        return;
    }
    char line[256];
    ESP_LOGI(TAG, "===== Content of %s =====", filename);
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;
        ESP_LOGI(TAG, "%s", line);
    }
    fclose(f);
    ESP_LOGI(TAG, "===== End of file =====");
}

void file_logger_check_new_day(void) {
    if (!time_synced_flag) return;
    char new_filename[64];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    get_filename_for_date(tm, new_filename, sizeof(new_filename));
    if (strcmp(new_filename, current_filename) != 0) {
        // Если старый файл существует – отправляем и удаляем
        FILE *old = fopen(current_filename, "r");
        if (old != NULL) {
            fclose(old);
            ESP_LOGI(TAG, "Sending and deleting previous day file %s", current_filename);
            send_file(current_filename);
            print_file_content(current_filename);
            vTaskDelay(pdMS_TO_TICKS(1000));
            delete_file(current_filename);
        } else {
            ESP_LOGI(TAG, "Old file %s does not exist, skipping", current_filename);
        }
        strcpy(current_filename, new_filename);
        ESP_LOGI(TAG, "Switched to new log file: %s", current_filename);
        moisture_sum = 0;
        temp_sum = 0;
        readings_count = 0;
    }
}

static void add_sensor_reading_and_send(void) {
    if (!time_synced_flag) {
        ESP_LOGW(TAG, "Time not synced yet, skipping 10-min record");
        return;
    }
    if (readings_count == 0) {
        ESP_LOGW(TAG, "No readings accumulated for 10-min record");
        return;
    }
    float avg_moisture = moisture_sum / readings_count;
    float avg_temp = temp_sum / readings_count;
    sensor_data_t data = sensors_read();
    FILE *f = fopen(current_filename, "a");
    if (f) {
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        char time_str[20];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm);
        fprintf(f, "%s;%.1f;%.2f;%d;%d\n", time_str, avg_moisture, avg_temp, data.level1, data.level2);
        fclose(f);
        ESP_LOGI(TAG, "10-min record added: %.1f%%, %.2f°C", avg_moisture, avg_temp);
        send_file(current_filename);
        print_file_content(current_filename);
    } else {
        ESP_LOGE(TAG, "Cannot open %s", current_filename);
    }
    moisture_sum = 0;
    temp_sum = 0;
    readings_count = 0;
}

static void schedule_next_record(void) {
    if (!time_synced_flag) return;
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    int minutes = tm->tm_min;
    int next_minute = ((minutes / 10) + 1) * 10;
    int sec_to_next = (next_minute - minutes) * 60 - tm->tm_sec;
    if (sec_to_next <= 0) sec_to_next += 600;
    int64_t delay_us = sec_to_next * 1000000LL;
    esp_timer_stop(log_timer);
    esp_timer_start_once(log_timer, delay_us);
    int next_hour = tm->tm_hour;
    int next_min = next_minute;
    if (next_min >= 60) {
        next_hour += next_min / 60;
        next_min %= 60;
    }
    if (next_hour >= 24) next_hour -= 24;
    ESP_LOGI(TAG, "Next 10-min record scheduled in %d seconds (at %02d:%02d:00)",
             sec_to_next, next_hour, next_min);
}

static void log_timer_callback(void *arg) {
    logger_cmd_t cmd = LOGGER_CMD_ADD_RECORD;
    xQueueSend(logger_queue, &cmd, 0);
}

static void logger_task(void *arg) {
    logger_cmd_t cmd;
    while (1) {
        if (xQueueReceive(logger_queue, &cmd, portMAX_DELAY)) {
            if (cmd == LOGGER_CMD_ADD_RECORD) {
                add_sensor_reading_and_send();
                file_logger_check_new_day();
                schedule_next_record();
            } else if (cmd == LOGGER_CMD_RESTART) {
                time_synced_flag = true;
                update_filename();
                ESP_LOGI(TAG, "Time synced, using log file: %s", current_filename);
                schedule_next_record();
            }
        }
    }
}

void file_logger_init(void) {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed");
        return;
    }
    ESP_LOGI(TAG, "File logger initialized, waiting for time sync");
    logger_queue = xQueueCreate(5, sizeof(logger_cmd_t));
    xTaskCreate(logger_task, "logger_task", 8192, NULL, 2, &logger_task_handle);
    const esp_timer_create_args_t timer_args = {
        .callback = log_timer_callback,
        .name = "log_timer"
    };
    esp_timer_create(&timer_args, &log_timer);
}

void file_logger_restart_timer(void) {
    if (logger_queue) {
        logger_cmd_t cmd = LOGGER_CMD_RESTART;
        xQueueSend(logger_queue, &cmd, 0);
    }
}

void file_logger_accumulate(float moisture, float temp) {
    if (!time_synced_flag) return;
    moisture_sum += moisture;
    temp_sum += temp;
    readings_count++;
}

void file_logger_log_event(const char *state, const char *device) {
    if (!time_synced_flag) return;
    FILE *f = fopen(current_filename, "a");
    if (!f) return;
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm);
    fprintf(f, "%s;EVENT;%s;%s\n", time_str, device, state);
    fclose(f);
    ESP_LOGI(TAG, "Event: %s %s", device, state);
}

void file_logger_send_file(void) {
    if (!time_synced_flag) return;
    send_file(current_filename);
    print_file_content(current_filename);
}

void file_logger_print_file(void) {
    if (!time_synced_flag) return;
    print_file_content(current_filename);
}