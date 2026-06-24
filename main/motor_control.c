#include "motor_control.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "sdkconfig.h"

static const char *TAG = "MotorControl";

// === ПИНЫ ИЗ KCONFIG ===
#define PUMP_ENA_GPIO       CONFIG_PUMP_PWM_GPIO   // 6
#define PUMP_IN1_GPIO       CONFIG_PUMP_IN1_GPIO   // 7
#define VALVE_GPIO          CONFIG_VALVE_GPIO      // 4

#define PUMP_PWM_CHANNEL    LEDC_CHANNEL_0
#define PUMP_PWM_TIMER      LEDC_TIMER_0
#define PUMP_PWM_FREQ       (1000)
#define PUMP_PWM_RES        LEDC_TIMER_10_BIT

static bool pump_running = false;
static bool valve_open = false;
static int current_speed = 30;

void motor_control_init(void) {
    // === ШИМ для насоса (ENA) ===
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = PUMP_PWM_TIMER,
        .duty_resolution = PUMP_PWM_RES,
        .freq_hz = PUMP_PWM_FREQ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_conf);

    ledc_channel_config_t ch_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = PUMP_PWM_CHANNEL,
        .timer_sel = PUMP_PWM_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = PUMP_ENA_GPIO,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&ch_conf);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, PUMP_PWM_CHANNEL, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, PUMP_PWM_CHANNEL);

    // === IN1 для включения насоса ===
    gpio_config_t in1_conf = {
        .pin_bit_mask = (1ULL << PUMP_IN1_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&in1_conf);
    gpio_set_level(PUMP_IN1_GPIO, 0);

    // === Клапан (IN3) ===
    gpio_config_t valve_conf = {
        .pin_bit_mask = (1ULL << VALVE_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&valve_conf);
    gpio_set_level(VALVE_GPIO, 0);

    ESP_LOGI(TAG, "Motor control initialized (ENA=GPIO%d, IN1=GPIO%d, VALVE=GPIO%d)",
             PUMP_ENA_GPIO, PUMP_IN1_GPIO, VALVE_GPIO);
}

void motor_pump_set_speed(int speed_percent) {
    if (speed_percent < 0) speed_percent = 0;
    if (speed_percent > 100) speed_percent = 100;
    current_speed = speed_percent;

    uint32_t duty = (uint32_t)(speed_percent * 1023 / 100);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, PUMP_PWM_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, PUMP_PWM_CHANNEL);

    ESP_LOGI(TAG, "Pump speed set to %d%% (duty %d)", speed_percent, duty);
}

void motor_pump_start(void) {
    if (current_speed == 0) {
        ESP_LOGW(TAG, "Cannot start pump: speed is 0%%");
        return;
    }
    gpio_set_level(PUMP_IN1_GPIO, 1);
    uint32_t duty = (uint32_t)(current_speed * 1023 / 100);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, PUMP_PWM_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, PUMP_PWM_CHANNEL);
    pump_running = true;
    ESP_LOGI(TAG, "Pump started at speed %d%%", current_speed);
}

void motor_pump_stop(void) {
    gpio_set_level(PUMP_IN1_GPIO, 0);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, PUMP_PWM_CHANNEL, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, PUMP_PWM_CHANNEL);
    pump_running = false;
    ESP_LOGI(TAG, "Pump stopped");
}

bool is_pump_running(void) {
    return pump_running;
}

int motor_pump_get_speed(void) {
    return current_speed;
}

void motor_valve_open(void) {
    gpio_set_level(VALVE_GPIO, 1);
    valve_open = true;
    ESP_LOGI(TAG, "Valve opened");
}

void motor_valve_close(void) {
    gpio_set_level(VALVE_GPIO, 0);
    valve_open = false;
    ESP_LOGI(TAG, "Valve closed");
}

bool is_valve_open(void) {
    return valve_open;
}