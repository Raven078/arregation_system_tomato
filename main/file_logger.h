#ifndef FILE_LOGGER_H
#define FILE_LOGGER_H

#include <stdbool.h>
#include <stdint.h>

void file_logger_init(void);
bool file_logger_get_latest_data(const char* greenhouse,
                                 float* temperature, int* moisture,
                                 bool* sensor1_detected, bool* sensor2_detected);
void file_logger_cleanup_old_logs(int days_keep);
void file_logger_update_chart_data(void);
void file_logger_update_chart_data_from_file(const char *filename);
void file_logger_get_chart_data(int32_t **temp_array, int32_t **humi_array, int *point_count);
void file_logger_get_pump_valve_data(int32_t **pump_array, int32_t **valve_array, int *point_count);
void file_logger_append_data(const char *greenhouse, float temperature, int moisture,
                             bool pump_state, bool valve_state);
void file_logger_log_event(const char *greenhouse, const char *component, const char *state);

// === НОВЫЕ ФУНКЦИИ ===
bool file_logger_is_today_file_exists(const char *greenhouse);
void file_logger_rotate_logs_if_needed(const char *greenhouse);

#endif