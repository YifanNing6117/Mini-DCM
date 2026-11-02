#include "minidcm_error.h"
#include "dtc_mgr.h"
#include "minidcm_event_log.h"

static MiniDcm_ErrorStateType s_minidcm_error_state;

void MiniDcm_Error_Init(void)
{
    s_minidcm_error_state.last_error = MINIDCM_E_NONE;
    s_minidcm_error_state.error_count = 0u;
    MiniDcm_EventLog_Init();
    DtcMgr_Init();
}

void MiniDcm_Error_Report(MiniDcm_ErrorCodeType error)
{
    if (error == MINIDCM_E_NONE)
    {
        return;
    }

    s_minidcm_error_state.last_error = error;
    s_minidcm_error_state.error_count++;
    MiniDcm_EventLog_Record(MINIDCM_EVENT_ERROR, error);
    DtcMgr_ReportError(error);
}

MiniDcm_ErrorCodeType MiniDcm_Error_GetLast(void)
{
    return s_minidcm_error_state.last_error;
}

uint32_t MiniDcm_Error_GetCount(void)
{
    return s_minidcm_error_state.error_count;
}

void MiniDcm_Error_Clear(void)
{
    s_minidcm_error_state.last_error = MINIDCM_E_NONE;
}
