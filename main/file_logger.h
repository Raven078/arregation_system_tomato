#pragma once

#include <stdbool.h>

void file_logger_init(void);
void file_logger_accumulate(float moisture, float temp);
void file_logger_log_event(const char *state, const char *device);
void file_logger_check_new_day(void);
void file_logger_send_file(void);
void file_logger_restart_timer(void);
void file_logger_print_file(void);