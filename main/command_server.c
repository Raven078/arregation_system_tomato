#include "command_server.h"
#include "motor_control.h"
#include "irrigation_logic.h"
#include "sensors.h"
#include "data_sender.h"
#include "file_logger.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "sdkconfig.h"
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/stat.h>

static const char *TAG = "CMD_SERVER";

#define COMMAND_PORT 8889
#define BUFFER_SIZE 4096   // буфер приёма

static int server_socket = -1;
static TaskHandle_t server_task_handle = NULL;

// Обработка команды — возвращает динамически выделенную строку ответа
static char* handle_command(const char *cmd, int *out_len) {
    char *response = NULL;
    int response_len = 0;

    // === Поддержка команды pump_speed:XX (без JSON) ===
    if (strncmp(cmd, "pump_speed:", 11) == 0) {
        int speed = atoi(cmd + 11);
        if (speed >= 0 && speed <= 100) {
            motor_pump_set_speed(speed);
            response = malloc(128);
            if (response) {
                snprintf(response, 128, "{\"status\":\"ok\",\"message\":\"Speed set to %d%%\"}", speed);
                response_len = strlen(response);
            }
        } else {
            response = malloc(64);
            if (response) {
                snprintf(response, 64, "{\"status\":\"error\",\"message\":\"Speed must be 0-100\"}");
                response_len = strlen(response);
            }
        }
        if (out_len) *out_len = response_len;
        return response;
    }

    // === Обычная обработка JSON-команд ===
    cJSON *root = cJSON_Parse(cmd);
    if (root == NULL) {
        response = malloc(64);
        if (response) {
            snprintf(response, 64, "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
            response_len = strlen(response);
        }
        if (out_len) *out_len = response_len;
        return response;
    }

    cJSON *command = cJSON_GetObjectItem(root, "command");
    if (command == NULL) {
        response = malloc(64);
        if (response) {
            snprintf(response, 64, "{\"status\":\"error\",\"message\":\"Missing 'command' field\"}");
            response_len = strlen(response);
        }
        cJSON_Delete(root);
        if (out_len) *out_len = response_len;
        return response;
    }

    const char *cmd_str = command->valuestring;
    cJSON *params = cJSON_GetObjectItem(root, "params");

    #define SET_RESPONSE(msg) do { \
        response = malloc(strlen(msg) + 1); \
        if (response) { strcpy(response, msg); response_len = strlen(response); } \
    } while(0)

    if (strcmp(cmd_str, "pump_start") == 0) {
        motor_pump_start();
        irrigation_logic_set_pump_state(true);
        SET_RESPONSE("{\"status\":\"ok\",\"message\":\"Pump started\"}");
    }
    else if (strcmp(cmd_str, "pump_stop") == 0) {
        motor_pump_stop();
        irrigation_logic_set_pump_state(false);
        SET_RESPONSE("{\"status\":\"ok\",\"message\":\"Pump stopped\"}");
    }
    else if (strcmp(cmd_str, "pump_speed") == 0) {
        if (params) {
            cJSON *speed = cJSON_GetObjectItem(params, "speed");
            if (speed && speed->type == cJSON_Number) {
                motor_pump_set_speed((int)speed->valuedouble);
                char buf[128];
                snprintf(buf, sizeof(buf), "{\"status\":\"ok\",\"message\":\"Speed set to %d%%\"}", (int)speed->valuedouble);
                SET_RESPONSE(buf);
            } else {
                SET_RESPONSE("{\"status\":\"error\",\"message\":\"Missing 'speed' parameter\"}");
            }
        } else {
            SET_RESPONSE("{\"status\":\"error\",\"message\":\"Missing params\"}");
        }
    }
    else if (strcmp(cmd_str, "valve_open") == 0) {
        motor_valve_open();
        irrigation_logic_set_valve_state(true);
        SET_RESPONSE("{\"status\":\"ok\",\"message\":\"Valve opened\"}");
    }
    else if (strcmp(cmd_str, "valve_close") == 0) {
        motor_valve_close();
        irrigation_logic_set_valve_state(false);
        SET_RESPONSE("{\"status\":\"ok\",\"message\":\"Valve closed\"}");
    }
    else if (strcmp(cmd_str, "send_data") == 0) {
        data_sender_request_data();
        SET_RESPONSE("{\"status\":\"ok\",\"message\":\"Data sending enabled\"}");
    }
    else if (strcmp(cmd_str, "stop_data") == 0) {
        data_sender_stop_data();
        SET_RESPONSE("{\"status\":\"ok\",\"message\":\"Data sending disabled\"}");
    }
    else if (strcmp(cmd_str, "status") == 0) {
        sensor_data_t data = sensors_read();
        char json_data[256];
        snprintf(json_data, sizeof(json_data),
                 "{\"moisture\":%d,\"temperature\":%.1f,\"level1\":%d,\"level2\":%d,\"pump\":\"%s\",\"valve\":\"%s\"}",
                 data.moisture_percent, data.temperature, data.level1, data.level2,
                 irrigation_logic_is_pump_active() ? "on" : "off",
                 irrigation_logic_is_valve_active() ? "on" : "off");
        char buf[512];
        snprintf(buf, sizeof(buf), "{\"status\":\"ok\",\"data\":%s}", json_data);
        SET_RESPONSE(buf);
    }
    else if (strcmp(cmd_str, "read_log") == 0) {
        time_t now = time(NULL);
        struct tm tm_info;
        localtime_r(&now, &tm_info);
        char filename[64];
        snprintf(filename, sizeof(filename), "/spiffs/%s_%d_%d_%d.txt",
                 CONFIG_DEVICE_NAME, tm_info.tm_mday, tm_info.tm_mon + 1, tm_info.tm_year + 1900);

        FILE *f = fopen(filename, "r");
        if (f == NULL) {
            SET_RESPONSE("{\"status\":\"error\",\"message\":\"Log file not found\"}");
        } else {
            fseek(f, 0, SEEK_END);
            long fsize = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (fsize > 16384) {
                fclose(f);
                SET_RESPONSE("{\"status\":\"error\",\"message\":\"Log file too large (>16KB)\"}");
            } else {
                char *content = malloc(fsize + 1);
                if (content == NULL) {
                    fclose(f);
                    SET_RESPONSE("{\"status\":\"error\",\"message\":\"Memory error\"}");
                } else {
                    size_t read_bytes = fread(content, 1, fsize, f);
                    content[read_bytes] = '\0';
                    fclose(f);
                    cJSON *resp = cJSON_CreateObject();
                    cJSON_AddStringToObject(resp, "status", "ok");
                    cJSON_AddStringToObject(resp, "content", content);
                    char *json_str = cJSON_PrintUnformatted(resp);
                    if (json_str) {
                        response = malloc(strlen(json_str) + 1);
                        if (response) {
                            strcpy(response, json_str);
                            response_len = strlen(response);
                        }
                        free(json_str);
                    }
                    cJSON_Delete(resp);
                    free(content);
                }
            }
        }
    }
    else {
        char buf[128];
        snprintf(buf, sizeof(buf), "{\"status\":\"error\",\"message\":\"Unknown command: %s\"}", cmd_str);
        SET_RESPONSE(buf);
    }

    cJSON_Delete(root);

    if (out_len) *out_len = response_len;
    return response;
}

// Задача сервера
static void command_server_task(void *pvParameters) {
    ESP_LOGI(TAG, "Command server started on port %d", COMMAND_PORT);

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(COMMAND_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        ESP_LOGE(TAG, "Unable to create socket");
        return;
    }

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Unable to bind socket");
        close(server_socket);
        return;
    }

    if (listen(server_socket, 5) < 0) {
        ESP_LOGE(TAG, "Unable to listen");
        close(server_socket);
        return;
    }

    ESP_LOGI(TAG, "Command server listening on port %d", COMMAND_PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket < 0) {
            ESP_LOGE(TAG, "Accept failed");
            continue;
        }

        char *rx_buffer = malloc(BUFFER_SIZE);
        if (!rx_buffer) {
            ESP_LOGE(TAG, "Failed to allocate rx buffer");
            close(client_socket);
            continue;
        }

        ssize_t len = recv(client_socket, rx_buffer, BUFFER_SIZE - 1, 0);
        if (len > 0) {
            rx_buffer[len] = '\0';
            ESP_LOGI(TAG, "Received command: %s", rx_buffer);

            int resp_len = 0;
            char *response = handle_command(rx_buffer, &resp_len);
            if (response && resp_len > 0) {
                send(client_socket, response, resp_len, 0);
                free(response);
            } else {
                const char *err = "{\"status\":\"error\",\"message\":\"Internal error\"}";
                send(client_socket, err, strlen(err), 0);
            }
        } else {
            ESP_LOGW(TAG, "recv error or client closed");
        }

        free(rx_buffer);
        close(client_socket);
    }
}

void command_server_start(void) {
    if (server_task_handle != NULL) {
        ESP_LOGW(TAG, "Command server already running");
        return;
    }
    BaseType_t ret = xTaskCreate(command_server_task, "command_server", 4096, NULL, 5, &server_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create command server task");
    }
}

void command_server_stop(void) {
    if (server_socket >= 0) {
        close(server_socket);
        server_socket = -1;
    }
    if (server_task_handle != NULL) {
        vTaskDelete(server_task_handle);
        server_task_handle = NULL;
    }
    ESP_LOGI(TAG, "Command server stopped");
}