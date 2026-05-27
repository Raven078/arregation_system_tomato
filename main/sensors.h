// main/sensors.h
#pragma once

#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "onewire_bus.h"
#include "ds18b20.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "sdkconfig.h"

// ==================== Конфигурация пинов ====================
#define HW390_GPIO             GPIO_NUM_1
#define ONE_WIRE_GPIO          GPIO_NUM_0
#define LEVEL_SENSOR_1_GPIO    GPIO_NUM_2
#define LEVEL_SENSOR_2_GPIO    GPIO_NUM_3

// ==================== Калибровка датчика влажности ====================
#define V_DRY_mV   2500
#define V_WET_mV   1000

// ==================== Имя устройства ====================
#ifdef CONFIG_DEVICE_NAME
    #define DEVICE_NAME CONFIG_DEVICE_NAME
#else
    #define DEVICE_NAME "Теплица помидорник"
#endif

// ==================== Структура для данных датчиков ====================
typedef struct {
    int moisture_percent;
    int raw_adc;
    int voltage_mV;
    float temperature;
    int level1;
    int level2;
} sensor_data_t;

// ==================== Прототипы функций ====================
void sensors_init(void);
sensor_data_t sensors_read(void);
char* sensors_create_json(const sensor_data_t *data, const char *device_name, const char *device_id);
void sensors_get_device_id(char *id_buffer, size_t buffer_size);