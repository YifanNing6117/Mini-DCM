#ifndef MINIDCM_TIMER_MGR_H
#define MINIDCM_TIMER_MGR_H

#include "minidcm_types.h"

typedef uint8_t MiniDcm_TimerIdType;

#define MINIDCM_TIMER_INVALID_ID ((MiniDcm_TimerIdType)0xFFu)

typedef enum
{
    MINIDCM_TIMER_STOPPED = 0u,
    MINIDCM_TIMER_RUNNING,
    MINIDCM_TIMER_EXPIRED
} MiniDcm_TimerStateType;

typedef struct
{
    uint32_t start_ms;
    uint32_t timeout_ms;
    MiniDcm_TimerStateType state;
} MiniDcm_TimerType;

typedef struct
{
    MiniDcm_TimerType *timers;
    MiniDcm_TimerIdType timer_count;
    uint8_t initialized;
} MiniDcm_TimerMgrType;

Std_ReturnType MiniDcm_TimerMgr_Init(MiniDcm_TimerMgrType *mgr,
                                     MiniDcm_TimerType *timers,
                                     MiniDcm_TimerIdType timer_count);

Std_ReturnType MiniDcm_TimerMgr_Start(MiniDcm_TimerMgrType *mgr,
                                      MiniDcm_TimerIdType timer_id,
                                      uint32_t now_ms,
                                      uint32_t timeout_ms);

Std_ReturnType MiniDcm_TimerMgr_Stop(MiniDcm_TimerMgrType *mgr,
                                     MiniDcm_TimerIdType timer_id);

MiniDcm_TimerStateType MiniDcm_TimerMgr_GetState(MiniDcm_TimerMgrType *mgr,
                                                 MiniDcm_TimerIdType timer_id,
                                                 uint32_t now_ms);

uint8_t MiniDcm_TimerMgr_IsExpired(MiniDcm_TimerMgrType *mgr,
                                   MiniDcm_TimerIdType timer_id,
                                   uint32_t now_ms);

uint32_t MiniDcm_TimerMgr_GetRemaining(MiniDcm_TimerMgrType *mgr,
                                       MiniDcm_TimerIdType timer_id,
                                       uint32_t now_ms);

#endif
