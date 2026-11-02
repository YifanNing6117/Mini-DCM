#ifndef ECU_APP_H
#define ECU_APP_H

#include <stdint.h>

void EcuApp_MainFunction(uint32_t now_ms);
uint32_t EcuApp_GetRxCount(void);

#endif
