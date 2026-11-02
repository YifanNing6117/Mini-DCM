#include "session_mgr.h"

#include "minidcm_timer_mgr.h"
#include "session_cfg.h"

#define SESSION_MGR_TIMER_S3 0u
#define SESSION_MGR_TIMER_COUNT 1u

static uint8_t s_session_mgr_current = SESSION_CFG_DEFAULT_SESSION;
static MiniDcm_TimerType s_session_mgr_timers[SESSION_MGR_TIMER_COUNT];
static MiniDcm_TimerMgrType s_session_mgr_timer_mgr;

void SessionMgr_Init(void)
{
    s_session_mgr_current = SESSION_CFG_DEFAULT_SESSION;
    (void)MiniDcm_TimerMgr_Init(&s_session_mgr_timer_mgr,
                                s_session_mgr_timers,
                                SESSION_MGR_TIMER_COUNT);
}

SessionMgr_StatusType SessionMgr_SetSession(uint8_t session, uint32_t now_ms)
{
    if (SessionCfg_IsSupported(session) == 0u)
    {
        return SESSION_MGR_ERR_UNSUPPORTED;
    }

    s_session_mgr_current = session;
    if (session == SESSION_CFG_DEFAULT_SESSION)
    {
        (void)MiniDcm_TimerMgr_Stop(&s_session_mgr_timer_mgr, SESSION_MGR_TIMER_S3);
    }
    else
    {
        (void)MiniDcm_TimerMgr_Start(&s_session_mgr_timer_mgr,
                                     SESSION_MGR_TIMER_S3,
                                     now_ms,
                                     SESSION_CFG_S3_TIMEOUT_MS);
    }

    return SESSION_MGR_OK;
}

void SessionMgr_MainFunction(uint32_t now_ms)
{
    if (s_session_mgr_current == SESSION_CFG_DEFAULT_SESSION)
    {
        return;
    }

    if (MiniDcm_TimerMgr_IsExpired(&s_session_mgr_timer_mgr, SESSION_MGR_TIMER_S3, now_ms) != 0u)
    {
        s_session_mgr_current = SESSION_CFG_DEFAULT_SESSION;
        (void)MiniDcm_TimerMgr_Stop(&s_session_mgr_timer_mgr, SESSION_MGR_TIMER_S3);
    }
}

void SessionMgr_OnUdsActivity(uint32_t now_ms)
{
    if (s_session_mgr_current != SESSION_CFG_DEFAULT_SESSION)
    {
        (void)MiniDcm_TimerMgr_Start(&s_session_mgr_timer_mgr,
                                     SESSION_MGR_TIMER_S3,
                                     now_ms,
                                     SESSION_CFG_S3_TIMEOUT_MS);
    }
}

uint8_t SessionMgr_GetSession(void)
{
    return s_session_mgr_current;
}
