#include "command_server.h"
#include "motor_control.h"
#include "data_sender.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <string.h>
#include <unistd.h>
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
    if (strcmp(rx_buffer, "pump_on") == 0) {
        motor_pump_set_speed(100);
        ESP_LOGI(TAG, "Pump turned ON");
    } else if (strcmp(rx_buffer, "pump_off") == 0) {
        motor_pump_stop();
        ESP_LOGI(TAG, "Pump turned OFF");
    } else if (strcmp(rx_buffer, "valve_open") == 0) {
        motor_valve_open();
        ESP_LOGI(TAG, "Valve opened");
    } else if (strcmp(rx_buffer, "valve_close") == 0) {
        motor_valve_close();
        ESP_LOGI(TAG, "Valve closed");
    } else if (strcmp(rx_buffer, "send_data") == 0) {
        data_sender_request_data();
        ESP_LOGI(TAG, "Data requested");
        response = "DATA_REQUESTED\n";
    } else if (strcmp(rx_buffer, "stop_data") == 0) {
        data_sender_stop_data();
        ESP_LOGI(TAG, "Data stop requested");
        response = "DATA_STOPPED\n";
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