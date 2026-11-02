#include "security_mgr.h"

#include "minidcm_event_log.h"
#include "minidcm_timer_mgr.h"
#include "security_cfg.h"

#define SECURITY_MGR_TIMER_LOCKOUT 0u
#define SECURITY_MGR_TIMER_COUNT 1u

static uint8_t s_security_unlocked;
static uint8_t s_security_failed_attempts;
static MiniDcm_TimerType s_security_timers[SECURITY_MGR_TIMER_COUNT];
static MiniDcm_TimerMgrType s_security_timer_mgr;

static uint8_t SecurityMgr_IsLocked(uint32_t now_ms)
{
    MiniDcm_TimerStateType lock_state;

    lock_state = MiniDcm_TimerMgr_GetState(&s_security_timer_mgr,
                                           SECURITY_MGR_TIMER_LOCKOUT,
                                           now_ms);
    if (lock_state == MINIDCM_TIMER_RUNNING)
    {
        return 1u;
    }

    if (lock_state == MINIDCM_TIMER_EXPIRED)
    {
        (void)MiniDcm_TimerMgr_Stop(&s_security_timer_mgr, SECURITY_MGR_TIMER_LOCKOUT);
        s_security_failed_attempts = 0u;
        return 0u;
    }

    return 0u;
}

void SecurityMgr_Init(void)
{
    s_security_unlocked = 0u;
    s_security_failed_attempts = 0u;
    (void)MiniDcm_TimerMgr_Init(&s_security_timer_mgr,
                                s_security_timers,
                                SECURITY_MGR_TIMER_COUNT);
}

SecurityMgr_StatusType SecurityMgr_RequestSeed(uint32_t now_ms, uint8_t *seed, uint8_t seed_len)
{
    uint8_t i;
    const uint8_t *configured_seed;

    if (SecurityMgr_IsLocked(now_ms) != 0u)
    {
        return SECURITY_MGR_ERR_LOCKED;
    }
    if ((seed == 0) || (seed_len < SECURITY_CFG_SEED_LEN))
    {
        return SECURITY_MGR_ERR_INVALID_KEY;
    }

    configured_seed = SecurityCfg_GetSeed();
    for (i = 0u; i < SECURITY_CFG_SEED_LEN; i++)
    {
        seed[i] = configured_seed[i];
    }

    return SECURITY_MGR_OK;
}

SecurityMgr_StatusType SecurityMgr_ValidateKey(uint32_t now_ms, const uint8_t *key, uint8_t key_len)
{
    uint8_t i;
    const uint8_t *configured_seed;

    if (SecurityMgr_IsLocked(now_ms) != 0u)
    {
        return SECURITY_MGR_ERR_LOCKED;
    }
    if ((key == 0) || (key_len != SECURITY_CFG_KEY_LEN))
    {
        return SECURITY_MGR_ERR_INVALID_KEY;
    }

    configured_seed = SecurityCfg_GetSeed();
    for (i = 0u; i < SECURITY_CFG_KEY_LEN; i++)
    {
        if (key[i] != SecurityCfg_CalcKeyByte(configured_seed[i]))
        {
            s_security_unlocked = 0u;
            s_security_failed_attempts++;
            if (s_security_failed_attempts >= SECURITY_CFG_MAX_FAILED_ATTEMPTS)
            {
                (void)MiniDcm_TimerMgr_Start(&s_security_timer_mgr,
                                             SECURITY_MGR_TIMER_LOCKOUT,
                                             now_ms,
                                             SECURITY_CFG_LOCK_TIME_MS);
                MiniDcm_EventLog_RecordWithTimestamp(
                    MINIDCM_EVENT_SECURITY_LOCKED,
                    s_security_failed_attempts,
                    now_ms);
            }
            return SECURITY_MGR_ERR_INVALID_KEY;
        }
    }

    s_security_unlocked = 1u;
    s_security_failed_attempts = 0u;
    (void)MiniDcm_TimerMgr_Stop(&s_security_timer_mgr, SECURITY_MGR_TIMER_LOCKOUT);
    MiniDcm_EventLog_RecordWithTimestamp(
        MINIDCM_EVENT_SECURITY_UNLOCKED,
        0u,
        now_ms);
    return SECURITY_MGR_OK;
}

uint8_t SecurityMgr_IsUnlocked(void)
{
    return s_security_unlocked;
}

uint8_t SecurityMgr_GetFailedAttempts(void)
{
    return s_security_failed_attempts;
}
