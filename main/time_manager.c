#include "time_manager.h"
#include "wifi_app.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include "sdkconfig.h"

static const char *TAG = "TimeManager";
static esp_timer_handle_t daily_sync_timer = NULL;

static void daily_sync_callback(void *arg) {
    ESP_LOGI(TAG, "Daily time sync...");
    time_sync_from_tcp();
}

bool time_sync_from_tcp(void) {
    if (!wifi_is_connected()) {
        ESP_LOGW(TAG, "WiFi not connected");
        return false;
    }
    ESP_LOGI(TAG, "Requesting timestamp...");
    if (tcp_send_data("GET_TIMESTAMP\n") != 0) {
        ESP_LOGE(TAG, "Failed to send request");
        return false;
    }
    char *resp = tcp_receive_data();
    if (!resp) {
        ESP_LOGE(TAG, "No response");
        return false;
    }
    ESP_LOGI(TAG, "Received: '%s'", resp);
    long long seconds = 0;
    // Пытаемся найти префикс "TIMESTAMP:"
    const char *prefix = "TIMESTAMP:";
    char *colon = strstr(resp, prefix);
    if (colon) {
        seconds = atoll(colon + strlen(prefix));
    } else {
        // Если префикса нет, пробуем преобразовать всю строку
        seconds = atoll(resp);
    }
    free(resp);
    if (seconds <= 0) {
        ESP_LOGE(TAG, "Invalid timestamp");
        return false;
    }
    struct timeval tv = { .tv_sec = seconds, .tv_usec = 0 };
    if (settimeofday(&tv, NULL) == 0) {
        time_t now = seconds;
        ESP_LOGI(TAG, "Time synchronized: %s", ctime(&now));
        return true;
    }
    ESP_LOGE(TAG, "Failed to set system time");
    return false;
}

void time_manager_init(void) {
    ESP_LOGI(TAG, "Init time manager");
    // Ежедневная синхронизация в 23:59:50
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    struct tm target = *tm;
    target.tm_hour = 23;
    target.tm_min = 59;
    target.tm_sec = 50;
    time_t target_sec = mktime(&target);
    if (target_sec <= now) {
        target_sec += 24 * 3600;
    }
    int64_t delay_us = (target_sec - now) * 1000000LL;
    const esp_timer_create_args_t timer_args = {
        .callback = daily_sync_callback,
        .name = "daily_sync"
    };
    esp_timer_create(&timer_args, &daily_sync_timer);
    esp_timer_start_once(daily_sync_timer, delay_us);
    ESP_LOGI(TAG, "Next daily sync in %lld seconds", delay_us / 1000000);
}

int get_current_hour(void) {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    return tm->tm_hour;
}