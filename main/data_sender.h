#ifndef DATA_SENDER_H
#define DATA_SENDER_H

#include <stdbool.h>

void data_sender_init(void);
void data_sender_accumulate(float moisture, float temp);
void data_sender_check_events(int level1, int level2, bool pump_running, bool valve_open);
void data_sender_request_data(void);
void data_sender_stop_data(void);
void data_sender_send_registration(void);   // для первичной регистрации

#endif