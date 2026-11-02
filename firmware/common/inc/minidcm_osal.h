#ifndef MINIDCM_OSAL_H
#define MINIDCM_OSAL_H

#include "minidcm_types.h"

typedef uint32_t (*MiniDcm_OsalGetTickFuncType)(void);
typedef void (*MiniDcm_OsalVoidFuncType)(void);

typedef struct
{
    MiniDcm_OsalGetTickFuncType get_tick_ms;
    MiniDcm_OsalVoidFuncType enter_critical;
    MiniDcm_OsalVoidFuncType exit_critical;
    void (*delay_ms)(uint32_t delay_ms);
} MiniDcm_OsalConfigType;

Std_ReturnType MiniDcm_Osal_Init(const MiniDcm_OsalConfigType *config);
uint8_t MiniDcm_Osal_IsInitialized(void);
uint32_t MiniDcm_Osal_GetTickMs(void);
void MiniDcm_Osal_DelayMs(uint32_t delay_ms);
void MiniDcm_Osal_EnterCritical(void);
void MiniDcm_Osal_ExitCritical(void);

#endif
