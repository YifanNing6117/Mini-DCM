#ifndef MINIDCM_APP_H
#define MINIDCM_APP_H

#include <stdint.h>
#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

void Minidcm_AppInit(UART_HandleTypeDef *uart, CAN_HandleTypeDef *can);
void Minidcm_AppPoll(void);
void Minidcm_AppOnRxByte(uint8_t byte);
void Minidcm_AppOnCanRx(const CAN_RxHeaderTypeDef *header, const uint8_t *data);
void Minidcm_AppOnCanError(void);
void Minidcm_AppOnUartIsrNotify(void);
void Minidcm_AppOnCanIsrNotify(void);
void Minidcm_AppOnBridgeTaskWakeup(void);
void Minidcm_AppOnHeartbeatTask(void);
void Minidcm_AppOnStackHighWaterMark(uint16_t bridge_words, uint16_t heartbeat_words);

#ifdef __cplusplus
}
#endif

#endif
