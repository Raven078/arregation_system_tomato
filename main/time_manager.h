#pragma once

#include <stdbool.h>

void time_manager_init(void);
bool time_sync_from_tcp(void);
int get_current_hour(void);