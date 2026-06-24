#include "button.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "Button";

#ifndef CONFIG_BUTTON_GPIO
#define CONFIG_BUTTON_GPIO 10
#endif

#define BUTTON_GPIO CONFIG_BUTTON_GPIO

static void button_task(void *pvParameters) {
    uint32_t last_time = 0;
    int last_state = 1;

    while (1) {
        int state = gpio_get_level(BUTTON_GPIO);
        uint32_t now = xTaskGetTickCount();

        if (state == 0 && last_state == 1 && (now - last_time) > pdMS_TO_TICKS(50)) {
            last_time = now;
            vTaskDelay(pdMS_TO_TICKS(50));
            if (gpio_get_level(BUTTON_GPIO) == 0) {
                ESP_LOGI(TAG, "Button pressed, restarting system...");
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
    ESP_LOGI(TAG, "Button initialized on GPIO %d (pull-up, press to restart)", BUTTON_GPIO);

    xTaskCreate(button_task, "button_task", 2048, NULL, 5, NULL);
}