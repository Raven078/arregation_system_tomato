#include "file_logger.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include "sdkconfig.h"

static const char *TAG = "FILE_LOGGER";
static const char *BASE_PATH = "/spiffs";

#define CHART_POINTS 144
static int32_t chart_temp[CHART_POINTS] = {0};
static int32_t chart_humi[CHART_POINTS] = {0};
static int32_t chart_pump[CHART_POINTS] = {0};
static int32_t chart_valve[CHART_POINTS] = {0};
static int chart_point_count = 0;

static time_t last_rotation_time = 0;

static int time_to_index(const char *time_str) {
    int hour, minute, second;
    if (sscanf(time_str, "%d:%d:%d", &hour, &minute, &second) != 3) return -1;
    int minutes_since_midnight = hour * 60 + minute;
    int index = minutes_since_midnight / 10;
    if (index < 0) index = 0;
    if (index >= CHART_POINTS) index = CHART_POINTS - 1;
    return index;
}

void file_logger_init(void) {
    ESP_LOGI(TAG, "Initializing SPIFFS");
    esp_vfs_spiffs_conf_t conf = {
        .base_path = BASE_PATH,
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS (%s)", esp_err_to_name(ret));
        return;
    }
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS info (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SPIFFS mounted. Total: %d, Used: %d", total, used);
    }
    last_rotation_time = time(NULL);
}

bool file_logger_get_latest_data(const char* greenhouse,
                                 float* temperature, int* moisture,
                                 bool* sensor1_detected, bool* sensor2_detected) {
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    char filename[64];
    const char *dev = (greenhouse && strlen(greenhouse) > 0) ? greenhouse : CONFIG_DEVICE_NAME;
    snprintf(filename, sizeof(filename), "/spiffs/%s_%d_%d_%d.txt",
             dev, tm_info.tm_mday, tm_info.tm_mon + 1, tm_info.tm_year + 1900);
    ESP_LOGD(TAG, "Reading log file: %s", filename);

    FILE *f = fopen(filename, "r");
    if (f == NULL) {
        ESP_LOGW(TAG, "Log file not found: %s", filename);
        return false;
    }

    char line[128];
    char *last_valid_line = NULL;
    while (fgets(line, sizeof(line), f) != NULL) {
        line[strcspn(line, "\r\n")] = 0;
        if (strlen(line) > 0) {
            if (last_valid_line) free(last_valid_line);
            last_valid_line = malloc(strlen(line) + 1);
            if (last_valid_line) strcpy(last_valid_line, line);
        }
    }
    fclose(f);

    if (last_valid_line == NULL) {
        ESP_LOGW(TAG, "No data lines found in file");
        return false;
    }

    char *saveptr;
    char *token = strtok_r(last_valid_line, ";", &saveptr);
    if (!token) { free(last_valid_line); return false; }
    token = strtok_r(NULL, ";", &saveptr);
    if (!token) { free(last_valid_line); return false; }
    token = strtok_r(NULL, ";", &saveptr);
    if (!token) { free(last_valid_line); return false; }
    float moist = atof(token);
    token = strtok_r(NULL, ";", &saveptr);
    if (!token) { free(last_valid_line); return false; }
    float temp = atof(token);
    token = strtok_r(NULL, ";", &saveptr);
    if (!token) { free(last_valid_line); return false; }
    int s1 = atoi(token);
    token = strtok_r(NULL, ";", &saveptr);
    if (!token) { free(last_valid_line); return false; }
    int s2 = atoi(token);

    *moisture = (int)moist;
    *temperature = temp;
    *sensor1_detected = (s1 == 1);
    *sensor2_detected = (s2 == 1);

    free(last_valid_line);
    return true;
}

void file_logger_cleanup_old_logs(int days_keep) {
    DIR *dir = opendir(BASE_PATH);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory %s: %s", BASE_PATH, strerror(errno));
        return;
    }

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        if (strncmp(name, CONFIG_DEVICE_NAME, strlen(CONFIG_DEVICE_NAME)) != 0) continue;
        size_t len = strlen(name);
        if (len < 5 || strcmp(name + len - 4, ".txt") != 0) continue;

        int day, month, year;
        if (sscanf(name, CONFIG_DEVICE_NAME "_%d_%d_%d.txt", &day, &month, &year) != 3) {
            ESP_LOGW(TAG, "Skipping file with unexpected name: %s", name);
            continue;
        }
        if (year < 2000 || year > 2100) {
            ESP_LOGW(TAG, "Invalid year in file name: %s", name);
            continue;
        }

        struct tm tm_file = {
            .tm_year = year - 1900,
            .tm_mon = month - 1,
            .tm_mday = day,
            .tm_hour = 0,
            .tm_min = 0,
            .tm_sec = 0,
            .tm_isdst = -1,
        };
        time_t file_time = mktime(&tm_file);
        if (file_time == -1) {
            ESP_LOGW(TAG, "Failed to convert file date for %s", name);
            continue;
        }

        double diff_days = difftime(now, file_time) / (60 * 60 * 24);
        if (diff_days > days_keep) {
            char full_path[512];
            int written = snprintf(full_path, sizeof(full_path), "%s/%s", BASE_PATH, name);
            if (written >= (int)sizeof(full_path)) {
                ESP_LOGW(TAG, "Path truncated: %s/%s", BASE_PATH, name);
                continue;
            }
            if (unlink(full_path) == 0) {
                ESP_LOGI(TAG, "Deleted old log file: %s (age %.1f days)", full_path, diff_days);
            } else {
                ESP_LOGE(TAG, "Failed to delete %s: %s", full_path, strerror(errno));
            }
        }
    }
    closedir(dir);
}

bool file_logger_is_today_file_exists(const char *greenhouse) {
    const char *dev = (greenhouse && strlen(greenhouse) > 0) ? greenhouse : CONFIG_DEVICE_NAME;
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    char filename[64];
    snprintf(filename, sizeof(filename), "/spiffs/%s_%d_%d_%d.txt",
             dev, tm_info.tm_mday, tm_info.tm_mon + 1, tm_info.tm_year + 1900);
    FILE *f = fopen(filename, "r");
    if (f) {
        fclose(f);
        return true;
    }
    return false;
}

void file_logger_rotate_logs_if_needed(const char *greenhouse) {
    const char *dev = (greenhouse && strlen(greenhouse) > 0) ? greenhouse : CONFIG_DEVICE_NAME;
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    // Проверяем, изменился ли день
    if (last_rotation_time != 0) {
        struct tm tm_last;
        localtime_r(&last_rotation_time, &tm_last);
        if (tm_now.tm_mday == tm_last.tm_mday &&
            tm_now.tm_mon == tm_last.tm_mon &&
            tm_now.tm_year == tm_last.tm_year) {
            return; // день не изменился
        }
    }

    // День изменился – удаляем все старые txt файлы
    DIR *dir = opendir(BASE_PATH);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            const char *name = entry->d_name;
            if (strncmp(name, dev, strlen(dev)) != 0) continue;
            if (strstr(name, ".txt") == NULL) continue;
            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s/%s", BASE_PATH, name);
            if (unlink(full_path) == 0) {
                ESP_LOGI(TAG, "Deleted old log file during rotation: %s", full_path);
            } else {
                ESP_LOGE(TAG, "Failed to delete %s: %s", full_path, strerror(errno));
            }
        }
        closedir(dir);
    }

    // Обновляем время последней ротации
    last_rotation_time = now;
    ESP_LOGI(TAG, "Log rotation completed for device %s", dev);
}

void file_logger_update_chart_data(void) {
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    char filename[64];
    snprintf(filename, sizeof(filename), "/spiffs/%s_%d_%d_%d.txt",
             CONFIG_DEVICE_NAME, tm_info.tm_mday, tm_info.tm_mon + 1, tm_info.tm_year + 1900);
    file_logger_update_chart_data_from_file(filename);
}

void file_logger_update_chart_data_from_file(const char *filename) {
    ESP_LOGI(TAG, "Updating chart data from file: %s", filename);

    memset(chart_temp, 0, sizeof(chart_temp));
    memset(chart_humi, 0, sizeof(chart_humi));
    memset(chart_pump, 0, sizeof(chart_pump));
    memset(chart_valve, 0, sizeof(chart_valve));
    chart_point_count = 0;

    FILE *f = fopen(filename, "r");
    if (!f) {
        ESP_LOGW(TAG, "File not found: %s", filename);
        return;
    }

    char line[256];
    bool pump_state = false;
    bool valve_state = false;

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (strlen(line) == 0) continue;

        char *saveptr;
        char *tokens[6];
        int token_count = 0;
        char *token = strtok_r(line, ";", &saveptr);
        while (token && token_count < 6) {
            tokens[token_count++] = token;
            token = strtok_r(NULL, ";", &saveptr);
        }
        if (token_count < 3) continue;

        if (strcmp(tokens[0], CONFIG_DEVICE_NAME) != 0) continue;

        if (strcmp(tokens[2], "EVENT") == 0) {
            if (token_count >= 5) {
                if (strcmp(tokens[3], "pump") == 0) {
                    pump_state = (strcmp(tokens[4], "on") == 0);
                } else if (strcmp(tokens[3], "valve") == 0) {
                    valve_state = (strcmp(tokens[4], "on") == 0);
                }
            }
            continue;
        }

        if (token_count >= 6) {
            char *space_pos = strchr(tokens[1], ' ');
            if (!space_pos) continue;
            char time_str[9];
            strncpy(time_str, space_pos + 1, 8);
            time_str[8] = '\0';

            int idx = time_to_index(time_str);
            if (idx < 0 || idx >= CHART_POINTS) continue;

            float moisture = atof(tokens[2]);
            float temperature = atof(tokens[3]);
            int s1 = atoi(tokens[4]);
            int s2 = atoi(tokens[5]);

            chart_temp[idx] = (int32_t)(temperature * 10);
            chart_humi[idx] = (int32_t)moisture;
            chart_pump[idx] = pump_state ? 80 : 0;
            chart_valve[idx] = valve_state ? 80 : 0;
            chart_point_count++;
        }
    }

    fclose(f);
    ESP_LOGI(TAG, "Chart data updated: %d valid points", chart_point_count);
}

void file_logger_get_chart_data(int32_t **temp_array, int32_t **humi_array, int *point_count) {
    *temp_array = chart_temp;
    *humi_array = chart_humi;
    *point_count = chart_point_count;
}

void file_logger_get_pump_valve_data(int32_t **pump_array, int32_t **valve_array, int *point_count) {
    *pump_array = chart_pump;
    *valve_array = chart_valve;
    *point_count = chart_point_count;
}

void file_logger_append_data(const char *greenhouse, float temperature, int moisture,
                             bool pump_state, bool valve_state) {
    // Перед записью проверяем ротацию
    file_logger_rotate_logs_if_needed(greenhouse);

    const char *dev = (greenhouse && strlen(greenhouse) > 0) ? greenhouse : CONFIG_DEVICE_NAME;
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    char filename[64];
    snprintf(filename, sizeof(filename), "/spiffs/%s_%d_%d_%d.txt",
             dev, tm_info.tm_mday, tm_info.tm_mon + 1, tm_info.tm_year + 1900);

    FILE *f = fopen(filename, "a");
    if (!f) {
        ESP_LOGE(TAG, "Cannot append to %s", filename);
        return;
    }
    char time_str[9];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", &tm_info);
    char date_str[11];
    strftime(date_str, sizeof(date_str), "%Y-%m-%d", &tm_info);
    fprintf(f, "%s;%s %s;%.1f;%.1f;%d;%d\n",
            dev, date_str, time_str,
            (float)moisture,
            temperature,
            pump_state ? 1 : 0,
            valve_state ? 1 : 0);
    fclose(f);
    ESP_LOGD(TAG, "Appended data to %s", filename);
}

void file_logger_log_event(const char *greenhouse, const char *component, const char *state) {
    const char *dev = (greenhouse && strlen(greenhouse) > 0) ? greenhouse : CONFIG_DEVICE_NAME;
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    char filename[64];
    snprintf(filename, sizeof(filename), "/spiffs/%s_%d_%d_%d.txt",
             dev, tm_info.tm_mday, tm_info.tm_mon + 1, tm_info.tm_year + 1900);
    FILE *f = fopen(filename, "a");
    if (!f) {
        ESP_LOGE(TAG, "Cannot append event to %s", filename);
        return;
    }
    char time_str[9];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", &tm_info);
    char date_str[11];
    strftime(date_str, sizeof(date_str), "%Y-%m-%d", &tm_info);
    fprintf(f, "%s;%s %s;EVENT;%s;%s\n",
            dev, date_str, time_str, component, state);
    fclose(f);
    ESP_LOGD(TAG, "Event logged: %s %s %s", dev, component, state);
}