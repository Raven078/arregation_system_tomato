#ifndef IRRIGATION_LOGIC_H
#define IRRIGATION_LOGIC_H

#include "sensors.h"

void irrigation_logic_init(void);
void irrigation_logic_set_threshold(int t);
void irrigation_logic_update(const sensor_data_t *data);

// === НОВЫЕ ФУНКЦИИ ДЛЯ РУЧНОГО УПРАВЛЕНИЯ ===
void irrigation_logic_set_pump_state(bool active);
void irrigation_logic_set_valve_state(bool active);
bool irrigation_logic_is_pump_active(void);
bool irrigation_logic_is_valve_active(void);

#endif