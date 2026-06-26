#ifndef OTA_RECOVERY_H
#define OTA_RECOVERY_H

#include <stdbool.h>
#include <stdint.h>   // <-- ДОБАВЛЕНО

#ifdef __cplusplus
extern "C" {
#endif

bool ota_recovery_should_enter(void);
void ota_recovery_set_flag(bool enable);
void ota_recovery_start_server(uint16_t port);
void ota_recovery_clear_flag(void);

#ifdef __cplusplus
}
#endif

#endif /* OTA_RECOVERY_H */