#ifndef MINIDCM_APP_H
#define MINIDCM_APP_H

#include <stdint.h>
#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

void Minidcm_AppInit(UART_HandleTypeDef *uart);
void Minidcm_AppPoll(void);
void Minidcm_AppOnRxByte(uint8_t byte);

#ifdef __cplusplus
}
#endif

#endif
