#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <stdbool.h>

bool time_sync_from_tcp(void);
int get_current_hour(void);
void time_manager_init(void);   // пустая или для инициализации (если нужно)

#endif