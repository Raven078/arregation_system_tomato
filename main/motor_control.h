#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include <stdbool.h>

void motor_control_init(void);
void motor_pump_set_speed(int speed_percent);
void motor_pump_start(void);
void motor_pump_stop(void);
bool is_pump_running(void);
int motor_pump_get_speed(void);

void motor_valve_open(void);
void motor_valve_close(void);
bool is_valve_open(void);

#endif