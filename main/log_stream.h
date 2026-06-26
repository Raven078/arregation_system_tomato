#ifndef LOG_STREAM_H
#define LOG_STREAM_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Инициализировать модуль стриминга логов
 */
void log_stream_init(void);

/**
 * @brief Включить/выключить стриминг логов
 * @param enabled true - включить, false - выключить
 * @param sock Сокет для отправки логов (если enabled)
 */
void log_stream_set_enabled(bool enabled, int sock);

/**
 * @brief Отправить лог-сообщение через стрим (если включён)
 * @param tag Тэг лога
 * @param format Формат сообщения
 * @param ... Аргументы форматирования
 */
void log_stream_send(const char *tag, const char *format, ...);

/**
 * @brief Проверить, включён ли стриминг
 */
bool log_stream_is_enabled(void);

#ifdef __cplusplus
}
#endif

#endif /* LOG_STREAM_H */