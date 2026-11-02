#include "minidcm_runtime_stats.h"

static MiniDcm_RuntimeStatsType s_runtime_stats;

void MiniDcm_RuntimeStats_Init(void)
{
    s_runtime_stats.ecu_rx_count = 0u;
    s_runtime_stats.ecu_accepted_request_count = 0u;
    s_runtime_stats.ecu_ignored_frame_count = 0u;
}

void MiniDcm_RuntimeStats_IncEcuRxCount(void)
{
    s_runtime_stats.ecu_rx_count++;
}

void MiniDcm_RuntimeStats_IncEcuAcceptedRequestCount(void)
{
    s_runtime_stats.ecu_accepted_request_count++;
}

void MiniDcm_RuntimeStats_IncEcuIgnoredFrameCount(void)
{
    s_runtime_stats.ecu_ignored_frame_count++;
}

MiniDcm_RuntimeStatsType MiniDcm_RuntimeStats_Get(void)
{
    return s_runtime_stats;
}
