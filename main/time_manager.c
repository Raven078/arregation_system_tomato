#include "time_manager.h"
#include "wifi_app.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <sys/time.h>
#include <time.h>
#include <string.h>

static const char *TAG = "TIME_MANAGER";
static const char *SERVER_IP = "192.168.4.1";
static const int SERVER_PORT = 8888;

static char* tcp_receive_time(void) {
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return NULL;
    }

    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(SERVER_PORT);

    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) != 0) {
        ESP_LOGE(TAG, "Socket connect failed errno=%d", errno);
        close(sock);
        return NULL;
    }

    char buffer[64];
    int len = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (len <= 0) {
        ESP_LOGE(TAG, "Failed to receive time, errno=%d", errno);
        close(sock);
        return NULL;
    }
    buffer[len] = '\0';
    close(sock);

    char *newline = strchr(buffer, '\n');
    if (newline) *newline = '\0';

    return strdup(buffer);
}

bool time_sync_from_tcp(void) {
    if (!wifi_is_connected()) {
        ESP_LOGE(TAG, "WiFi not connected, cannot sync time");
        return false;
    }

    char *time_str = tcp_receive_time();
    if (!time_str) {
        ESP_LOGE(TAG, "Failed to get time from server");
        return false;
    }

    int day, month, year, hour, minute, second;
    if (sscanf(time_str, "%d.%d.%d %d:%d:%d", &day, &month, &year, &hour, &minute, &second) == 6) {
        struct tm tm = {
            .tm_year = year - 1900,
            .tm_mon = month - 1,
            .tm_mday = day,
            .tm_hour = hour,
            .tm_min = minute,
            .tm_sec = second,
            .tm_isdst = -1
        };
        time_t t = mktime(&tm);
        struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
        if (settimeofday(&tv, NULL) == 0) {
            ESP_LOGI(TAG, "System time set to: %s", time_str);
            free(time_str);
            return true;
        } else {
            ESP_LOGE(TAG, "Failed to set system time");
        }
    } else {
        ESP_LOGE(TAG, "Invalid time format received: %s", time_str);
    }
    free(time_str);
    return false;
}

int get_current_hour(void) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    if (tm_info == NULL) return -1;
    return tm_info->tm_hour;
}

void time_manager_init(void) {
    // Пустая функция, так как синхронизация уже выполнена в main.c
    // Можно добавить необходимую инициализацию, если потребуется в будущем
    ESP_LOGI(TAG, "Time manager initialized");
}