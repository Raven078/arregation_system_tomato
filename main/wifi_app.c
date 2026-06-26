#include "wifi_app.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include "data_sender.h"   // <-- ДОБАВЛЕНО

static const char *TAG = "WiFi";
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#define WIFI_SSID     CONFIG_WIFI_SSID
#define WIFI_PASSWORD CONFIG_WIFI_PASSWORD
#define TCP_SERVER_IP CONFIG_TCP_SERVER_IP
#define TCP_SERVER_PORT CONFIG_TCP_SERVER_PORT
#define COMMAND_PORT  CONFIG_COMMAND_PORT

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Connecting to WiFi...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, retrying...");
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        wifi_send_command_port();
    }
}

void wifi_init_sta(void) {
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi STA initialized");
}

bool wifi_is_connected(void) {
    if (!wifi_event_group) return false;
    EventBits_t bits = xEventGroupGetBits(wifi_event_group);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

int tcp_send_data(const char *json_str) {
    if (!json_str) return -1;
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Socket creation failed");
        return -1;
    }
    struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(TCP_SERVER_PORT);
    inet_pton(AF_INET, TCP_SERVER_IP, &dest_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) != 0) {
        ESP_LOGE(TAG, "Connect failed");
        close(sock);
        return -1;
    }
    size_t len = strlen(json_str);
    int sent = send(sock, json_str, len, 0);
    if (sent < 0) ESP_LOGE(TAG, "Send failed");
    close(sock);
    return (sent >= 0) ? 0 : -1;
}

void wifi_send_command_port(void) {
    char port_msg[32];
    snprintf(port_msg, sizeof(port_msg), "CMD_PORT:%d\n", COMMAND_PORT);
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Socket creation failed for port sending");
        return;
    }
    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(TCP_SERVER_PORT);
    inet_pton(AF_INET, TCP_SERVER_IP, &dest_addr.sin_addr);
    if (connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) == 0) {
        send(sock, port_msg, strlen(port_msg), 0);
        ESP_LOGI(TAG, "Command port %d sent to server", COMMAND_PORT);
        // Читаем ответ (время)
        char time_buf[32];
        int len = recv(sock, time_buf, sizeof(time_buf) - 1, 2000);
        if (len > 0) {
            time_buf[len] = '\0';
            char *newline = strchr(time_buf, '\n');
            if (newline) *newline = '\0';
            char *cr = strchr(time_buf, '\r');
            if (cr) *cr = '\0';
            ESP_LOGI(TAG, "Received time from server: %s", time_buf);
            struct tm tm = {0};
            if (sscanf(time_buf, "%d.%d.%d %d:%d:%d",
                       &tm.tm_mday, &tm.tm_mon, &tm.tm_year,
                       &tm.tm_hour, &tm.tm_min, &tm.tm_sec) == 6) {
                tm.tm_mon -= 1;
                tm.tm_year -= 1900;
                time_t t = mktime(&tm);
                struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
                settimeofday(&tv, NULL);
                ESP_LOGI(TAG, "System time set to: %s", time_buf);
                // === НОВЫЙ ВЫЗОВ ===
                data_sender_check_and_create_log();
            } else {
                ESP_LOGW(TAG, "Failed to parse time: %s", time_buf);
            }
        } else {
            ESP_LOGW(TAG, "No time response from server");
        }
    } else {
        ESP_LOGE(TAG, "Connect to server failed");
    }
    close(sock);
}