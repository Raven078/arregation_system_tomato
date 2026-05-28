#pragma once

#include <stdbool.h>

void motor_control_init(void);
void motor_pump_set_speed(int duty_cycle); // 0-100%
void motor_pump_stop(void);
void motor_valve_open(void);
void motor_valve_close(void);
bool is_pump_running(void);
bool is_valve_open(void);