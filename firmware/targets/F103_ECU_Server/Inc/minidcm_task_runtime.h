#ifndef MINIDCM_TASK_RUNTIME_H
#define MINIDCM_TASK_RUNTIME_H

#include "minidcm_types.h"

typedef void (*MiniDcm_TaskRuntimeHeartbeatHookType)(void);

typedef struct
{
    uint32_t dcm_task_count;
    uint32_t monitor_task_count;
    uint32_t heartbeat_task_count;
    uint32_t dcm_stack_high_water_mark;
    uint32_t monitor_stack_high_water_mark;
    uint32_t heartbeat_stack_high_water_mark;
    uint32_t dcm_task_last_run_age_ms;
    uint32_t monitor_task_last_run_age_ms;
    uint32_t heartbeat_task_last_run_age_ms;
    uint32_t can_rx_notification_count;
    uint32_t dcm_notification_wakeup_count;
    uint32_t can_rx_queue_push_count;
    uint32_t can_rx_queue_reject_count;
    uint32_t can_rx_queue_last_pending_count;
    uint32_t can_rx_queue_high_water_mark;
    uint8_t task_alive_mask;
    uint8_t task_fault_mask;
} MiniDcm_TaskRuntimeStatsType;

#define MINIDCM_TASK_ALIVE_DCM       0x01u
#define MINIDCM_TASK_ALIVE_MONITOR   0x02u
#define MINIDCM_TASK_ALIVE_HEARTBEAT 0x04u
#define MINIDCM_TASK_ALIVE_ALL       (MINIDCM_TASK_ALIVE_DCM | \
                                      MINIDCM_TASK_ALIVE_MONITOR | \
                                      MINIDCM_TASK_ALIVE_HEARTBEAT)

void MiniDcm_TaskRuntime_DcmOnce(void);
void MiniDcm_TaskRuntime_MonitorOnce(void);
void MiniDcm_TaskRuntime_HeartbeatOnce(MiniDcm_TaskRuntimeHeartbeatHookType heartbeat_hook);
void MiniDcm_TaskRuntime_UpdateDcmStackHighWaterMark(uint32_t high_water_mark);
void MiniDcm_TaskRuntime_UpdateMonitorStackHighWaterMark(uint32_t high_water_mark);
void MiniDcm_TaskRuntime_UpdateHeartbeatStackHighWaterMark(uint32_t high_water_mark);
void MiniDcm_TaskRuntime_OnCanRxNotificationFromIsr(void);
void MiniDcm_TaskRuntime_OnCanRxQueuePushFromIsr(uint32_t pending_count);
void MiniDcm_TaskRuntime_OnCanRxQueuePushRejectedFromIsr(void);
void MiniDcm_TaskRuntime_RecordDcmNotificationWakeup(void);
MiniDcm_TaskRuntimeStatsType MiniDcm_TaskRuntime_GetStats(void);

#endif
