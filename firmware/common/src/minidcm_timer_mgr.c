#include "minidcm_timer_mgr.h"

#include "minidcm_error.h"

static uint8_t MiniDcm_TimerMgr_IsValidTimer(MiniDcm_TimerMgrType *mgr,
                                             MiniDcm_TimerIdType timer_id)
{
    if ((mgr == NULL) || (mgr->initialized == FALSE) || (mgr->timers == NULL))
    {
        MiniDcm_Error_Report(MINIDCM_E_NOT_INIT);
        return FALSE;
    }

    if (timer_id >= mgr->timer_count)
    {
        MiniDcm_Error_Report(MINIDCM_E_PARAM);
        return FALSE;
    }

    return TRUE;
}

Std_ReturnType MiniDcm_TimerMgr_Init(MiniDcm_TimerMgrType *mgr,
                                     MiniDcm_TimerType *timers,
                                     MiniDcm_TimerIdType timer_count)
{
    MiniDcm_TimerIdType i;

    if ((mgr == NULL) || (timers == NULL) || (timer_count == 0u))
    {
        MiniDcm_Error_Report(MINIDCM_E_PARAM);
        return E_NOT_OK;
    }

    mgr->timers = timers;
    mgr->timer_count = timer_count;
    mgr->initialized = TRUE;

    for (i = 0u; i < timer_count; i++)
    {
        mgr->timers[i].start_ms = 0u;
        mgr->timers[i].timeout_ms = 0u;
        mgr->timers[i].state = MINIDCM_TIMER_STOPPED;
    }

    return E_OK;
}

Std_ReturnType MiniDcm_TimerMgr_Start(MiniDcm_TimerMgrType *mgr,
                                      MiniDcm_TimerIdType timer_id,
                                      uint32_t now_ms,
                                      uint32_t timeout_ms)
{
    if (MiniDcm_TimerMgr_IsValidTimer(mgr, timer_id) == FALSE)
    {
        return E_NOT_OK;
    }

    if (timeout_ms == 0u)
    {
        MiniDcm_Error_Report(MINIDCM_E_PARAM);
        return E_NOT_OK;
    }

    mgr->timers[timer_id].start_ms = now_ms;
    mgr->timers[timer_id].timeout_ms = timeout_ms;
    mgr->timers[timer_id].state = MINIDCM_TIMER_RUNNING;

    return E_OK;
}

Std_ReturnType MiniDcm_TimerMgr_Stop(MiniDcm_TimerMgrType *mgr,
                                     MiniDcm_TimerIdType timer_id)
{
    if (MiniDcm_TimerMgr_IsValidTimer(mgr, timer_id) == FALSE)
    {
        return E_NOT_OK;
    }

    mgr->timers[timer_id].start_ms = 0u;
    mgr->timers[timer_id].timeout_ms = 0u;
    mgr->timers[timer_id].state = MINIDCM_TIMER_STOPPED;

    return E_OK;
}

MiniDcm_TimerStateType MiniDcm_TimerMgr_GetState(MiniDcm_TimerMgrType *mgr,
                                                 MiniDcm_TimerIdType timer_id,
                                                 uint32_t now_ms)
{
    MiniDcm_TimerType *timer;

    if (MiniDcm_TimerMgr_IsValidTimer(mgr, timer_id) == FALSE)
    {
        return MINIDCM_TIMER_STOPPED;
    }

    timer = &mgr->timers[timer_id];
    if (timer->state != MINIDCM_TIMER_RUNNING)
    {
        return timer->state;
    }

    if ((uint32_t)(now_ms - timer->start_ms) >= timer->timeout_ms)
    {
        timer->state = MINIDCM_TIMER_EXPIRED;
    }

    return timer->state;
}

uint8_t MiniDcm_TimerMgr_IsExpired(MiniDcm_TimerMgrType *mgr,
                                   MiniDcm_TimerIdType timer_id,
                                   uint32_t now_ms)
{
    return (MiniDcm_TimerMgr_GetState(mgr, timer_id, now_ms) == MINIDCM_TIMER_EXPIRED) ? TRUE : FALSE;
}

uint32_t MiniDcm_TimerMgr_GetRemaining(MiniDcm_TimerMgrType *mgr,
                                       MiniDcm_TimerIdType timer_id,
                                       uint32_t now_ms)
{
    MiniDcm_TimerType *timer;
    uint32_t elapsed;

    if (MiniDcm_TimerMgr_IsValidTimer(mgr, timer_id) == FALSE)
    {
        return 0u;
    }

    timer = &mgr->timers[timer_id];
    if (timer->state != MINIDCM_TIMER_RUNNING)
    {
        return 0u;
    }

    elapsed = (uint32_t)(now_ms - timer->start_ms);
    if (elapsed >= timer->timeout_ms)
    {
        timer->state = MINIDCM_TIMER_EXPIRED;
        return 0u;
    }

    return (uint32_t)(timer->timeout_ms - elapsed);
}
