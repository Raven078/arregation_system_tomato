#pragma once

#include "sensors.h"

void irrigation_logic_init(void);
void irrigation_logic_update(const sensor_data_t *data);
void irrigation_logic_set_threshold(int threshold);