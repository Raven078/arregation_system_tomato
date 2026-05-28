#include "motor_control.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "MotorControl";
#define PUMP_PWM_PIN    GPIO_NUM_6
#define PUMP_IN1_PIN    GPIO_NUM_7
#define VALVE_IN3_PIN   GPIO_NUM_8

static bool pump_run = false;
static bool valve_open = false;

void motor_control_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PUMP_IN1_PIN) | (1ULL << VALVE_IN3_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(PUMP_IN1_PIN, 0);
    gpio_set_level(VALVE_IN3_PIN, 0);

    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .gpio_num = PUMP_PWM_PIN,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&ledc_channel);

    ESP_LOGI(TAG, "Motor control initialized");
}

void motor_pump_set_speed(int duty_cycle) {
    if (duty_cycle < 0) duty_cycle = 0;
    if (duty_cycle > 100) duty_cycle = 100;
    uint32_t duty = (duty_cycle * ((1 << LEDC_TIMER_13_BIT) - 1)) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    if (duty_cycle > 0) {
        gpio_set_level(PUMP_IN1_PIN, 1);
        pump_run = true;
        ESP_LOGI(TAG, "Pump ON, speed %d%%", duty_cycle);
    } else {
        gpio_set_level(PUMP_IN1_PIN, 0);
        pump_run = false;
        ESP_LOGI(TAG, "Pump OFF");
    }
}

void motor_pump_stop(void) {
    motor_pump_set_speed(0);
}

void motor_valve_open(void) {
    gpio_set_level(VALVE_IN3_PIN, 1);
    valve_open = true;
    ESP_LOGI(TAG, "Valve OPEN");
}

void motor_valve_close(void) {
    gpio_set_level(VALVE_IN3_PIN, 0);
    valve_open = false;
    ESP_LOGI(TAG, "Valve CLOSED");
}

bool is_pump_running(void) {
    return pump_run;
}

bool is_valve_open(void) {
    return valve_open;
}