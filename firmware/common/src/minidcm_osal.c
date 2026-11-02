#include "minidcm_osal.h"

#include "minidcm_error.h"

static MiniDcm_OsalConfigType s_osal_config;
static uint8_t s_osal_initialized;

static uint32_t MiniDcm_Osal_DefaultGetTickMs(void)
{
    return 0u;
}

static void MiniDcm_Osal_DefaultVoidHook(void)
{
}

Std_ReturnType MiniDcm_Osal_Init(const MiniDcm_OsalConfigType *config)
{
    if ((config == NULL) || (config->get_tick_ms == NULL))
    {
        MiniDcm_Error_Report(MINIDCM_E_PARAM);
        return E_NOT_OK;
    }

    s_osal_config.get_tick_ms = config->get_tick_ms;
    s_osal_config.enter_critical = (config->enter_critical != NULL)
                                       ? config->enter_critical
                                       : MiniDcm_Osal_DefaultVoidHook;
    s_osal_config.exit_critical = (config->exit_critical != NULL)
                                      ? config->exit_critical
                                      : MiniDcm_Osal_DefaultVoidHook;
    s_osal_config.delay_ms = config->delay_ms;
    s_osal_initialized = TRUE;

    return E_OK;
}

uint8_t MiniDcm_Osal_IsInitialized(void)
{
    return s_osal_initialized;
}

uint32_t MiniDcm_Osal_GetTickMs(void)
{
    if (s_osal_initialized == FALSE)
    {
        MiniDcm_Error_Report(MINIDCM_E_NOT_INIT);
        return MiniDcm_Osal_DefaultGetTickMs();
    }

    return s_osal_config.get_tick_ms();
}

void MiniDcm_Osal_DelayMs(uint32_t delay_ms)
{
    uint32_t start_ms;

    if (s_osal_initialized == FALSE)
    {
        MiniDcm_Error_Report(MINIDCM_E_NOT_INIT);
        return;
    }

    if (s_osal_config.delay_ms != NULL)
    {
        s_osal_config.delay_ms(delay_ms);
        return;
    }

    start_ms = s_osal_config.get_tick_ms();
    while ((uint32_t)(s_osal_config.get_tick_ms() - start_ms) < delay_ms)
    {
    }
}

void MiniDcm_Osal_EnterCritical(void)
{
    if (s_osal_initialized == FALSE)
    {
        MiniDcm_Error_Report(MINIDCM_E_NOT_INIT);
        return;
    }

    s_osal_config.enter_critical();
}

void MiniDcm_Osal_ExitCritical(void)
{
    if (s_osal_initialized == FALSE)
    {
        MiniDcm_Error_Report(MINIDCM_E_NOT_INIT);
        return;
    }

    s_osal_config.exit_critical();
}
