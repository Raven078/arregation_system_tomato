#include "log_stream.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h> 

static const char *TAG = "LOG_STREAM";
static bool s_enabled = false;
static int s_sock = -1;

void log_stream_init(void)
{
    ESP_LOGI(TAG, "Log stream initialized");
}

void log_stream_set_enabled(bool enabled, int sock)
{
    s_enabled = enabled;
    s_sock = sock;
    ESP_LOGI(TAG, "Log stream %s", enabled ? "enabled" : "disabled");
}

bool log_stream_is_enabled(void)
{
    return s_enabled;
}

void log_stream_send(const char *tag, const char *format, ...)
{
    if (!s_enabled || s_sock < 0) return;

    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    char line[600];
    snprintf(line, sizeof(line), "[%s] %s\n", tag, buffer);

    // Отправляем в сокет
    send(s_sock, line, strlen(line), 0);
}