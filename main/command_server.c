#include "command_server.h"
#include "motor_control.h"
#include "data_sender.h"
#include "sensors.h"
#include "irrigation_logic.h"
#include "file_logger.h"
#include "log_stream.h"
#include "ota_recovery.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <string.h>
#include <stdlib.h>
#include "sdkconfig.h"

static const char *TAG = "CMD_SERVER";
#define COMMAND_PORT CONFIG_COMMAND_PORT
#define MAX_CLIENTS 1

static void handle_command(int sock) {
    char rx_buffer[64];
    int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
    if (len <= 0) return;
    rx_buffer[len] = '\0';
    rx_buffer[strcspn(rx_buffer, "\r\n")] = 0;

    ESP_LOGI(TAG, "Received command: %s", rx_buffer);
    const char *response = "OK\n";

    // --- Команды управления ---
    if (strcmp(rx_buffer, "pump_on") == 0) {
        sensor_data_t data = sensors_read();
        if (data.level2 != 1) {
            ESP_LOGW(TAG, "Cannot turn pump ON: no water (level2=%d)", data.level2);
            file_logger_log_event(CONFIG_DEVICE_NAME, "pump", "off");
            response = "ERROR: No water\n";
        } else {
            motor_pump_start();
            irrigation_logic_set_pump_state(true);
            ESP_LOGI(TAG, "Pump turned ON (manual, speed %d%%)", motor_pump_get_speed());
            data_sender_check_events(data.level1, data.level2, true, irrigation_logic_is_valve_active());
            response = "OK\n";
        }
    } else if (strcmp(rx_buffer, "pump_off") == 0) {
        motor_pump_stop();
        irrigation_logic_set_pump_state(false);
        ESP_LOGI(TAG, "Pump turned OFF (manual)");
        sensor_data_t data = sensors_read();
        data_sender_check_events(data.level1, data.level2, false, irrigation_logic_is_valve_active());
        response = "OK\n";
    } else if (strcmp(rx_buffer, "valve_open") == 0) {
        sensor_data_t data = sensors_read();
        if (data.level1 == 1) {
            ESP_LOGW(TAG, "Cannot open valve: upper level detected (level1=1)");
            file_logger_log_event(CONFIG_DEVICE_NAME, "valve", "off");
            response = "ERROR: Tank full\n";
        } else {
            motor_valve_open();
            irrigation_logic_set_valve_state(true);
            ESP_LOGI(TAG, "Valve opened (manual)");
            data_sender_check_events(data.level1, data.level2, irrigation_logic_is_pump_active(), true);
            response = "OK\n";
        }
    } else if (strcmp(rx_buffer, "valve_close") == 0) {
        motor_valve_close();
        irrigation_logic_set_valve_state(false);
        ESP_LOGI(TAG, "Valve closed (manual)");
        sensor_data_t data = sensors_read();
        data_sender_check_events(data.level1, data.level2, irrigation_logic_is_pump_active(), false);
        response = "OK\n";
    } else if (strncmp(rx_buffer, "pump_speed:", 11) == 0) {
        int speed = atoi(rx_buffer + 11);
        if (speed < 0) speed = 0;
        if (speed > 100) speed = 100;
        motor_pump_set_speed(speed);
        ESP_LOGI(TAG, "Pump speed set to %d%%", speed);
        response = "OK\n";
    } else if (strcmp(rx_buffer, "send_data") == 0) {
        data_sender_request_data();
        response = "DATA_REQUESTED\n";
    } else if (strcmp(rx_buffer, "stop_data") == 0) {
        data_sender_stop_data();
        response = "DATA_STOPPED\n";
    } else if (strcmp(rx_buffer, "start_log_stream") == 0) {
        log_stream_set_enabled(true, sock);
        response = "OK\n";
    } else if (strcmp(rx_buffer, "stop_log_stream") == 0) {
        log_stream_set_enabled(false, -1);
        response = "OK\n";
    } else {
        response = "ERROR: unknown command\n";
        ESP_LOGW(TAG, "Unknown command: %s", rx_buffer);
    }

    send(sock, response, strlen(response), 0);
}

static void command_server_task(void *pvParameters) {
    int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket");
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(COMMAND_PORT)
    };

    if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "Bind failed");
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    if (listen(listen_sock, MAX_CLIENTS) != 0) {
        ESP_LOGE(TAG, "Listen failed");
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Command server started on port %d", COMMAND_PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock < 0) {
            ESP_LOGE(TAG, "Accept failed");
            continue;
        }
        handle_command(client_sock);
        shutdown(client_sock, 0);
        close(client_sock);
    }
}

void command_server_start(void) {
    xTaskCreate(command_server_task, "cmd_server", 4096, NULL, 5, NULL);
}