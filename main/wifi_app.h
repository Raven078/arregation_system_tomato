// main/wifi_app.h
#pragma once

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <string.h>

// Event group bits
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

// Прототипы функций
void wifi_init_sta(void);
bool wifi_is_connected(void);
int tcp_send_data(const char *json_str);
char* tcp_receive_data(void);