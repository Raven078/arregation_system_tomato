#ifndef WIFI_APP_H
#define WIFI_APP_H

#include <stdbool.h>

void wifi_init_sta(void);
bool wifi_is_connected(void);
int tcp_send_data(const char *json_str);
void wifi_send_command_port(void);

#endif