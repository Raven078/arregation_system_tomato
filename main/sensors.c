// main/sensors.c
#include "sensors.h"

// Определение калибровочных констант
const int V_DRY_mV = CONFIG_DRY_MV;
const int V_WET_mV = CONFIG_WET_MV;

static const char *TAG = "Sensors";

static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t cali_handle = NULL;
static onewire_bus_handle_t owb_bus = NULL;
static ds18b20_device_handle_t ds18b20_dev = NULL;

static bool adc_calibration_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cali_config, out_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Калибровка АЦП не удалась – используется прямое преобразование");
        *out_handle = NULL;
        return false;
    }
    ESP_LOGI(TAG, "Калибровка АЦП успешно инициализирована");
    return true;
}

static int voltage_to_moisture_percent(int voltage_mV)
{
    if (voltage_mV >= V_DRY_mV) return 0;
    if (voltage_mV <= V_WET_mV) return 100;
    return (int)(100.0f * (V_DRY_mV - voltage_mV) / (V_DRY_mV - V_WET_mV));
}

void sensors_get_device_id(char *id_buffer, size_t buffer_size)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);  // ИСПРАВЛЕНО: используем esp_read_mac
    snprintf(id_buffer, buffer_size, "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void sensors_init(void)
{
    ESP_LOGI(TAG, "Начало инициализации датчиков...");

    // --- АЦП для датчика влажности (HW-390) ---
    adc_oneshot_unit_init_cfg_t adc_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_1, &chan_cfg));

    adc_calibration_init(ADC_UNIT_1, ADC_ATTEN_DB_12, &cali_handle);
    ESP_LOGI(TAG, "АЦП для HW-390 инициализирован (GPIO %d)", HW390_GPIO);

    // --- Цифровые входы для датчиков уровня (XKC-Y25-V) ---
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LEVEL_SENSOR_1_GPIO) | (1ULL << LEVEL_SENSOR_2_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    ESP_LOGI(TAG, "Датчики уровня инициализированы (GPIO %d, %d)", 
             LEVEL_SENSOR_1_GPIO, LEVEL_SENSOR_2_GPIO);

    // --- 1-Wire шина и датчик температуры DS18B20 ---
    onewire_bus_config_t owb_config = {
        .bus_gpio_num = ONE_WIRE_GPIO,
        .flags = { .en_pull_up = true },
    };
    onewire_bus_rmt_config_t rmt_config = {
        .max_rx_bytes = 10,
    };
    ESP_ERROR_CHECK(onewire_new_bus_rmt(&owb_config, &rmt_config, &owb_bus));
    ESP_LOGI(TAG, "1-Wire шина инициализирована (GPIO %d)", ONE_WIRE_GPIO);

    // Поиск DS18B20
    onewire_device_iter_handle_t iter = NULL;
    onewire_device_t next_dev;
    ESP_ERROR_CHECK(onewire_new_device_iter(owb_bus, &iter));
    ESP_LOGI(TAG, "Поиск устройств на 1-Wire шине...");

    if (onewire_device_iter_get_next(iter, &next_dev) == ESP_OK) {
        ds18b20_config_t ds_cfg = {};
        if (ds18b20_new_device_from_enumeration(&next_dev, &ds_cfg, &ds18b20_dev) == ESP_OK) {
            ESP_LOGI(TAG, "DS18B20 успешно обнаружен и готов к работе");
        } else {
            ESP_LOGE(TAG, "Обнаруженное устройство не является DS18B20!");
            ds18b20_dev = NULL;
        }
    } else {
        ESP_LOGW(TAG, "DS18B20 не обнаружен на шине! Проверьте подключение.");
        ds18b20_dev = NULL;
    }
    ESP_ERROR_CHECK(onewire_del_device_iter(iter));
    
    ESP_LOGI(TAG, "Инициализация всех датчиков завершена");
}

sensor_data_t sensors_read(void)
{
    sensor_data_t data = {0};

    // Датчик влажности
    adc_oneshot_read(adc_handle, ADC_CHANNEL_1, &data.raw_adc);

    if (cali_handle != NULL) {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, data.raw_adc, &data.voltage_mV));
    } else {
        data.voltage_mV = (data.raw_adc * 3300) / 4095;
    }

    data.moisture_percent = voltage_to_moisture_percent(data.voltage_mV);

    // Датчик температуры
    if (ds18b20_dev != NULL) {
        ds18b20_trigger_temperature_conversion_for_all(owb_bus);
        vTaskDelay(pdMS_TO_TICKS(750));
        ds18b20_get_temperature(ds18b20_dev, &data.temperature);
    } else {
        data.temperature = -127.0f;
    }

    // Датчики уровня
    data.level1 = gpio_get_level(LEVEL_SENSOR_1_GPIO);
    data.level2 = gpio_get_level(LEVEL_SENSOR_2_GPIO);

    return data;
}

char* sensors_create_json(const sensor_data_t *data, const char *device_name, const char *device_id)
{
    cJSON *root = cJSON_CreateObject();
    
    // Информация об устройстве
    cJSON *device = cJSON_CreateObject();
    cJSON_AddStringToObject(device, "name", device_name);
    cJSON_AddStringToObject(device, "id", device_id);
    cJSON_AddStringToObject(device, "type", "ESP32-C3_Sensor_Monitor");
    cJSON_AddItemToObject(root, "device", device);

    // Временная метка
    cJSON_AddNumberToObject(root, "timestamp", (double)esp_log_timestamp() / 1000.0);

    // Данные сенсоров
    cJSON *sensors = cJSON_CreateObject();

    // Влажность
    cJSON *moisture = cJSON_CreateObject();
    cJSON_AddNumberToObject(moisture, "percent", data->moisture_percent);
    cJSON_AddNumberToObject(moisture, "raw", data->raw_adc);
    cJSON_AddNumberToObject(moisture, "voltage_mv", data->voltage_mV);
    cJSON_AddStringToObject(moisture, "unit", "%");
    cJSON_AddItemToObject(sensors, "moisture", moisture);

    // Температура
    cJSON *temperature = cJSON_CreateObject();
    cJSON_AddNumberToObject(temperature, "value", data->temperature);
    cJSON_AddStringToObject(temperature, "unit", "°C");
    cJSON_AddItemToObject(sensors, "temperature", temperature);

    // Уровни жидкости
    cJSON *levels = cJSON_CreateObject();
    
    cJSON *level1 = cJSON_CreateObject();
    cJSON_AddBoolToObject(level1, "detected", data->level1 ? true : false);
    cJSON_AddStringToObject(level1, "status", data->level1 ? "detected" : "not_detected");
    cJSON_AddItemToObject(levels, "sensor_1", level1);
    
    cJSON *level2 = cJSON_CreateObject();
    cJSON_AddBoolToObject(level2, "detected", data->level2 ? true : false);
    cJSON_AddStringToObject(level2, "status", data->level2 ? "detected" : "not_detected");
    cJSON_AddItemToObject(levels, "sensor_2", level2);
    
    cJSON_AddItemToObject(sensors, "levels", levels);

    cJSON_AddItemToObject(root, "sensors", sensors);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    return json_str;
}