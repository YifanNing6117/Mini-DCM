#include "session_cfg.h"

static const uint8_t s_session_cfg_supported_sessions[] =
{
    SESSION_CFG_DEFAULT_SESSION,
    SESSION_CFG_PROGRAMMING_SESSION,
    SESSION_CFG_EXTENDED_SESSION
};

uint8_t SessionCfg_IsSupported(uint8_t session)
{
    uint8_t i;

    for (i = 0u; i < (uint8_t)(sizeof(s_session_cfg_supported_sessions) / sizeof(s_session_cfg_supported_sessions[0])); i++)
    {
        if (s_session_cfg_supported_sessions[i] == session)
        {
            return 1u;
        }
    }

    return 0u;
}
