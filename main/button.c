#include "button.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "ota_recovery.h"
#include "log_stream.h"

static const char *TAG = "Button";

#ifndef CONFIG_BUTTON_GPIO
#define CONFIG_BUTTON_GPIO 10
#endif

#define BUTTON_GPIO CONFIG_BUTTON_GPIO
#define LONG_PRESS_MS 5000   // 5 секунд

static void button_task(void *pvParameters) {
    uint32_t press_start = 0;
    int last_state = 1;  // HIGH (подтяжка к VCC)
    bool long_press_triggered = false;

    while (1) {
        int state = gpio_get_level(BUTTON_GPIO);
        uint32_t now = xTaskGetTickCount();

        if (state == 0 && last_state == 1) {
            // Кнопка нажата
            press_start = now;
            long_press_triggered = false;
            ESP_LOGD(TAG, "Button pressed");
        } else if (state == 0 && last_state == 0) {
            // Кнопка удерживается
            if (!long_press_triggered && (now - press_start) >= pdMS_TO_TICKS(LONG_PRESS_MS)) {
                long_press_triggered = true;
                ESP_LOGI(TAG, "Button long press (5s) detected, entering OTA recovery mode");
                ota_recovery_set_flag(true);
                log_stream_send("BUTTON", "Long press detected, entering OTA recovery mode");
                vTaskDelay(pdMS_TO_TICKS(500));
                esp_restart();
            }
        } else if (state == 1 && last_state == 0) {
            // Кнопка отпущена
            if (!long_press_triggered) {
                // Короткое нажатие (<5 сек) – обычный рестарт
                ESP_LOGI(TAG, "Button short press, restarting system...");
                vTaskDelay(pdMS_TO_TICKS(100));
                esp_restart();
            }
        }
        last_state = state;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void button_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    ESP_LOGI(TAG, "Button initialized on GPIO %d (short press restart, long press 5s -> OTA recovery)", BUTTON_GPIO);

    xTaskCreate(button_task, "button_task", 2048, NULL, 5, NULL);
}