#include "log_manager.h"
#include "wifi_app.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

static const char *TAG = "LogManager";
static esp_timer_handle_t log_timer = NULL;

static void send_log_callback(void *arg) {
    if (!wifi_is_connected()) {
        ESP_LOGW(TAG, "WiFi not connected, cannot send log");
        return;
    }
    const char *log_msg = "LOG: periodic log entry";
    if (tcp_send_data(log_msg) == 0) {
        ESP_LOGI(TAG, "Log sent");
    } else {
        ESP_LOGE(TAG, "Failed to send log");
    }
}

void log_manager_init(void) {
    ESP_LOGI(TAG, "Log manager initialized");
}

void log_manager_start(void) {
    int interval_min = CONFIG_LOG_SEND_INTERVAL_MIN;
    const esp_timer_create_args_t args = {
        .callback = send_log_callback,
        .name = "log_timer"
    };
    esp_timer_create(&args, &log_timer);
    esp_timer_start_periodic(log_timer, interval_min * 60 * 1000000ULL);
    ESP_LOGI(TAG, "Log timer started, interval=%d min", interval_min);
}

void log_send_file(void) {
    send_log_callback(NULL);
}