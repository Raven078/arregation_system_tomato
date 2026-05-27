// main/wifi_app.c
#include "wifi_app.h"
#include "sdkconfig.h"

static const char *TAG = "WiFi";

static EventGroupHandle_t wifi_event_group;
static int wifi_retry_count = 0;
static const int WIFI_MAX_RETRY = 5;

// Конфигурация с проверками
#ifdef CONFIG_WIFI_SSID
    #define WIFI_SSID CONFIG_WIFI_SSID
#else
    #define WIFI_SSID "ESP32-S3-Hotspot"
#endif

#ifdef CONFIG_WIFI_PASSWORD
    #define WIFI_PASSWORD CONFIG_WIFI_PASSWORD
#else
    #define WIFI_PASSWORD "12345678"
#endif

#ifdef CONFIG_TCP_SERVER_IP
    #define TCP_SERVER_IP CONFIG_TCP_SERVER_IP
#else
    #define TCP_SERVER_IP "192.168.4.1"
#endif

#ifdef CONFIG_TCP_SERVER_PORT
    #define TCP_SERVER_PORT CONFIG_TCP_SERVER_PORT
#else
    #define TCP_SERVER_PORT 8888
#endif

// Обработчик событий WiFi
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "Подключение к WiFi...");
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
                if (wifi_retry_count < WIFI_MAX_RETRY) {
                    ESP_LOGW(TAG, "Разрыв соединения. Попытка переподключения %d/%d...",
                             wifi_retry_count + 1, WIFI_MAX_RETRY);
                    esp_wifi_connect();
                    wifi_retry_count++;
                } else {
                    ESP_LOGE(TAG, "Достигнут лимит попыток подключения");
                    xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
                }
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Получен IP-адрес: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_retry_count = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

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

    ESP_LOGI(TAG, "WiFi STA инициализирован");
}

bool wifi_is_connected(void)
{
    if (wifi_event_group == NULL) return false;
    EventBits_t bits = xEventGroupGetBits(wifi_event_group);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

int tcp_send_data(const char *json_str)
{
    if (json_str == NULL) return -1;

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Не удалось создать сокет: %d", errno);
        return -1;
    }

    struct timeval tv = {
        .tv_sec = 3,
        .tv_usec = 0,
    };
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in dest_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(TCP_SERVER_PORT),
    };
    inet_pton(AF_INET, TCP_SERVER_IP, &dest_addr.sin_addr);

    ESP_LOGI(TAG, "Подключение к серверу %s:%d...", TCP_SERVER_IP, TCP_SERVER_PORT);

    if (connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) != 0) {
        ESP_LOGE(TAG, "Ошибка подключения к серверу: %d", errno);
        close(sock);
        return -1;
    }

    size_t len = strlen(json_str);
    int sent = send(sock, json_str, len, 0);
    if (sent < 0) {
        ESP_LOGE(TAG, "Ошибка отправки данных: %d", errno);
    } else {
        ESP_LOGI(TAG, "Отправлено %d байт на сервер", sent);
    }

    close(sock);
    return (sent >= 0) ? 0 : -1;
}