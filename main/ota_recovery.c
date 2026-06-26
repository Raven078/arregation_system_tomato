#include "ota_recovery.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>   // для ntohl, htonl

static const char *TAG = "OTA_RECOVERY";
#define OTA_RECOVERY_FLAG_NAMESPACE "storage"
#define OTA_RECOVERY_FLAG_KEY "ota_force"

void ota_recovery_set_flag(bool enable)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(OTA_RECOVERY_FLAG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %d", err);
        return;
    }
    err = nvs_set_u8(handle, OTA_RECOVERY_FLAG_KEY, enable ? 1 : 0);
    if (err == ESP_OK) {
        nvs_commit(handle);
    }
    nvs_close(handle);
    ESP_LOGI(TAG, "OTA recovery flag %s", enable ? "set" : "cleared");
}

bool ota_recovery_should_enter(void)
{
    nvs_handle_t handle;
    uint8_t val = 0;
    if (nvs_open(OTA_RECOVERY_FLAG_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_u8(handle, OTA_RECOVERY_FLAG_KEY, &val);
        nvs_close(handle);
    }
    return (val == 1);
}

void ota_recovery_clear_flag(void)
{
    ota_recovery_set_flag(false);
}

static void ota_recovery_task(void *pvParameters)
{
    uint16_t port = *((uint16_t*)pvParameters);
    free(pvParameters);

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
        .sin_port = htons(port)
    };

    if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "Bind failed");
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    if (listen(listen_sock, 1) != 0) {
        ESP_LOGE(TAG, "Listen failed");
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "OTA recovery server started on port %d", port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock < 0) {
            ESP_LOGE(TAG, "Accept failed");
            continue;
        }

        char client_ip[16];
        inet_ntoa_r(client_addr.sin_addr, client_ip, sizeof(client_ip));
        ESP_LOGI(TAG, "OTA recovery connection from %s", client_ip);

        // Получаем размер прошивки (4 байта, big-endian)
        uint32_t firmware_size = 0;
        int recv_len = recv(client_sock, &firmware_size, 4, 0);
        if (recv_len != 4) {
            ESP_LOGE(TAG, "Failed to receive firmware size");
            close(client_sock);
            continue;
        }
        firmware_size = ntohl(firmware_size);
        ESP_LOGI(TAG, "Firmware size: %u bytes", firmware_size);

        if (firmware_size == 0 || firmware_size > 2 * 1024 * 1024) {
            ESP_LOGE(TAG, "Invalid firmware size");
            close(client_sock);
            continue;
        }

        // Начинаем OTA
        const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);
        if (!ota_partition) {
            ESP_LOGE(TAG, "No OTA partition");
            close(client_sock);
            continue;
        }

        esp_ota_handle_t ota_handle;
        esp_err_t err = esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "OTA begin failed: %d", err);
            close(client_sock);
            continue;
        }

        ESP_LOGI(TAG, "OTA partition: %s", ota_partition->label);

        uint8_t buffer[1024];
        uint32_t remaining = firmware_size;
        bool success = true;
        while (remaining > 0) {
            int to_read = (remaining > 1024) ? 1024 : remaining;
            int len = recv(client_sock, buffer, to_read, 0);
            if (len <= 0) {
                ESP_LOGE(TAG, "Receive failed");
                success = false;
                break;
            }
            err = esp_ota_write(ota_handle, buffer, len);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "OTA write failed: %d", err);
                success = false;
                break;
            }
            remaining -= len;
            ESP_LOGD(TAG, "Received %d bytes, remaining %u", len, remaining);
        }

        if (success) {
            err = esp_ota_end(ota_handle);
            if (err == ESP_OK) {
                esp_ota_set_boot_partition(ota_partition);
                ESP_LOGI(TAG, "OTA update successful, rebooting...");
                send(client_sock, "OK", 2, 0);
                ota_recovery_clear_flag();
                close(client_sock);
                close(listen_sock);
                vTaskDelay(pdMS_TO_TICKS(500));
                esp_restart();
            } else {
                ESP_LOGE(TAG, "OTA end failed: %d", err);
                esp_ota_abort(ota_handle);
            }
        } else {
            esp_ota_abort(ota_handle);
            ESP_LOGE(TAG, "OTA aborted");
        }

        close(client_sock);
    }
}

void ota_recovery_start_server(uint16_t port)
{
    uint16_t *port_ptr = malloc(sizeof(uint16_t));
    *port_ptr = port;
    xTaskCreate(ota_recovery_task, "ota_recovery", 8192, port_ptr, 5, NULL);
}