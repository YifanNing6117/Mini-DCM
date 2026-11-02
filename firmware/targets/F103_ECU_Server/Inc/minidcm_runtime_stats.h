#ifndef MINIDCM_RUNTIME_STATS_H
#define MINIDCM_RUNTIME_STATS_H

#include "minidcm_types.h"

typedef struct
{
    uint32_t ecu_rx_count;
    uint32_t ecu_accepted_request_count;
    uint32_t ecu_ignored_frame_count;
} MiniDcm_RuntimeStatsType;

void MiniDcm_RuntimeStats_Init(void);
void MiniDcm_RuntimeStats_IncEcuRxCount(void);
void MiniDcm_RuntimeStats_IncEcuAcceptedRequestCount(void);
void MiniDcm_RuntimeStats_IncEcuIgnoredFrameCount(void);
MiniDcm_RuntimeStatsType MiniDcm_RuntimeStats_Get(void);

#endif
