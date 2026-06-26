#include "log_manager.h"
#include "wifi_app.h"
#include "file_logger.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "sdkconfig.h"

static const char *TAG = "LogManager";
static esp_timer_handle_t log_timer = NULL;
static QueueHandle_t log_queue = NULL;
static TaskHandle_t log_task_handle = NULL;

typedef enum {
    LOG_CMD_SEND_FILE
} log_cmd_t;

static void log_task(void *arg) {
    log_cmd_t cmd;
    while (1) {
        if (xQueueReceive(log_queue, &cmd, portMAX_DELAY)) {
            if (cmd == LOG_CMD_SEND_FILE) {
                if (!wifi_is_connected()) {
                    ESP_LOGW(TAG, "WiFi not connected, cannot send log");
                    continue;
                }
                file_logger_send_file();  // здесь уже будут логи
            }
        }
    }
}

static void send_log_callback(void *arg) {
    log_cmd_t cmd = LOG_CMD_SEND_FILE;
    xQueueSend(log_queue, &cmd, 0);
}

void log_manager_init(void) {
    ESP_LOGI(TAG, "Log manager initialized");
    log_queue = xQueueCreate(5, sizeof(log_cmd_t));
    xTaskCreate(log_task, "log_task", 4096, NULL, 2, &log_task_handle);
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
    if (log_queue) {
        log_cmd_t cmd = LOG_CMD_SEND_FILE;
        xQueueSend(log_queue, &cmd, 0);
    }
}