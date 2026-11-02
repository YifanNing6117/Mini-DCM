#include "minidcm_task_runtime.h"

#include "ecu_app.h"
#include "minidcm_error.h"
#include "minidcm_osal.h"
#include "minidcm_task_profile.h"
#include "sensor_app.h"

static volatile uint32_t s_dcm_task_count;
static volatile uint32_t s_monitor_task_count;
static volatile uint32_t s_heartbeat_task_count;
static volatile uint32_t s_dcm_stack_high_water_mark;
static volatile uint32_t s_monitor_stack_high_water_mark;
static volatile uint32_t s_heartbeat_stack_high_water_mark;
static volatile uint32_t s_dcm_task_last_run_ms;
static volatile uint32_t s_monitor_task_last_run_ms;
static volatile uint32_t s_heartbeat_task_last_run_ms;
static volatile uint32_t s_can_rx_notification_count;
static volatile uint32_t s_dcm_notification_wakeup_count;
static volatile uint32_t s_can_rx_queue_push_count;
static volatile uint32_t s_can_rx_queue_reject_count;
static volatile uint32_t s_can_rx_queue_last_pending_count;
static volatile uint32_t s_can_rx_queue_high_water_mark;
static volatile uint8_t s_task_seen_mask;

static uint32_t MiniDcm_TaskRuntime_GetAgeMs(uint32_t now_ms, uint32_t last_run_ms, uint8_t seen)
{
    if (seen == FALSE)
    {
        return UINT32_MAX;
    }

    return (uint32_t)(now_ms - last_run_ms);
}

static uint8_t MiniDcm_TaskRuntime_IsAlive(uint32_t now_ms,
                                           uint32_t last_run_ms,
                                           uint8_t seen,
                                           MiniDcm_TaskIdType task_id)
{
    const MiniDcm_TaskProfileType *profile = MiniDcm_TaskProfile_Get(task_id);

    if ((seen == FALSE) || (profile == 0))
    {
        return FALSE;
    }

    return (MiniDcm_TaskRuntime_GetAgeMs(now_ms, last_run_ms, seen) <= profile->health_timeout_ms)
               ? TRUE
               : FALSE;
}

static uint8_t MiniDcm_TaskRuntime_GetAliveMask(uint32_t now_ms,
                                                uint32_t dcm_last_run_ms,
                                                uint32_t monitor_last_run_ms,
                                                uint32_t heartbeat_last_run_ms,
                                                uint8_t seen_mask)
{
    uint8_t alive_mask = 0u;

    if (MiniDcm_TaskRuntime_IsAlive(now_ms, dcm_last_run_ms,
                                    (uint8_t)(seen_mask & MINIDCM_TASK_ALIVE_DCM),
                                    MINIDCM_TASK_DCM) != FALSE)
    {
        alive_mask |= MINIDCM_TASK_ALIVE_DCM;
    }

    if (MiniDcm_TaskRuntime_IsAlive(now_ms, monitor_last_run_ms,
                                    (uint8_t)(seen_mask & MINIDCM_TASK_ALIVE_MONITOR),
                                    MINIDCM_TASK_MONITOR) != FALSE)
    {
        alive_mask |= MINIDCM_TASK_ALIVE_MONITOR;
    }

    if (MiniDcm_TaskRuntime_IsAlive(now_ms, heartbeat_last_run_ms,
                                    (uint8_t)(seen_mask & MINIDCM_TASK_ALIVE_HEARTBEAT),
                                    MINIDCM_TASK_HEARTBEAT) != FALSE)
    {
        alive_mask |= MINIDCM_TASK_ALIVE_HEARTBEAT;
    }

    return alive_mask;
}

void MiniDcm_TaskRuntime_DcmOnce(void)
{
    uint32_t now_ms;

    if (MiniDcm_Osal_IsInitialized() == FALSE)
    {
        MiniDcm_Error_Report(MINIDCM_E_NOT_INIT);
        return;
    }

    now_ms = MiniDcm_Osal_GetTickMs();
    MiniDcm_Osal_EnterCritical();
    s_dcm_task_count++;
    s_dcm_task_last_run_ms = now_ms;
    s_task_seen_mask |= MINIDCM_TASK_ALIVE_DCM;
    MiniDcm_Osal_ExitCritical();
    EcuApp_MainFunction(now_ms);
}

void MiniDcm_TaskRuntime_MonitorOnce(void)
{
    uint32_t now_ms;

    if (MiniDcm_Osal_IsInitialized() == FALSE)
    {
        MiniDcm_Error_Report(MINIDCM_E_NOT_INIT);
        return;
    }

    /*
     * Reserved for the first RTOS migration step.
     *
     * Current V2.1 monitor data is updated by the protocol path itself:
     * RuntimeStats, EventLog, and DtcMgr are already touched by UDS/CAN
     * processing. A dedicated monitor task is kept as an explicit extension
     * point for future DTC aging, stack watermark checks, sensor sampling,
     * and heartbeat-like diagnostics.
    */
    now_ms = MiniDcm_Osal_GetTickMs();
    MiniDcm_Osal_EnterCritical();
    s_monitor_task_count++;
    s_monitor_task_last_run_ms = now_ms;
    s_task_seen_mask |= MINIDCM_TASK_ALIVE_MONITOR;
    MiniDcm_Osal_ExitCritical();
    SensorApp_MainFunction(now_ms);
}

void MiniDcm_TaskRuntime_HeartbeatOnce(MiniDcm_TaskRuntimeHeartbeatHookType heartbeat_hook)
{
    uint32_t now_ms;

    if (MiniDcm_Osal_IsInitialized() == FALSE)
    {
        MiniDcm_Error_Report(MINIDCM_E_NOT_INIT);
        return;
    }

    now_ms = MiniDcm_Osal_GetTickMs();
    MiniDcm_Osal_EnterCritical();
    s_heartbeat_task_count++;
    s_heartbeat_task_last_run_ms = now_ms;
    s_task_seen_mask |= MINIDCM_TASK_ALIVE_HEARTBEAT;
    MiniDcm_Osal_ExitCritical();

    if (heartbeat_hook != 0)
    {
        heartbeat_hook();
    }
}

void MiniDcm_TaskRuntime_UpdateDcmStackHighWaterMark(uint32_t high_water_mark)
{
    MiniDcm_Osal_EnterCritical();
    s_dcm_stack_high_water_mark = high_water_mark;
    MiniDcm_Osal_ExitCritical();
}

void MiniDcm_TaskRuntime_UpdateMonitorStackHighWaterMark(uint32_t high_water_mark)
{
    MiniDcm_Osal_EnterCritical();
    s_monitor_stack_high_water_mark = high_water_mark;
    MiniDcm_Osal_ExitCritical();
}

void MiniDcm_TaskRuntime_UpdateHeartbeatStackHighWaterMark(uint32_t high_water_mark)
{
    MiniDcm_Osal_EnterCritical();
    s_heartbeat_stack_high_water_mark = high_water_mark;
    MiniDcm_Osal_ExitCritical();
}

void MiniDcm_TaskRuntime_OnCanRxNotificationFromIsr(void)
{
    /*
     * This function is intentionally ISR-safe: CAN RX is the only writer,
     * and the FreeRTOS target masks this ISR while a task snapshots stats.
     */
    s_can_rx_notification_count++;
}

void MiniDcm_TaskRuntime_OnCanRxQueuePushFromIsr(uint32_t pending_count)
{
    s_can_rx_queue_push_count++;
    s_can_rx_queue_last_pending_count = pending_count;
    if (pending_count > s_can_rx_queue_high_water_mark)
    {
        s_can_rx_queue_high_water_mark = pending_count;
    }
}

void MiniDcm_TaskRuntime_OnCanRxQueuePushRejectedFromIsr(void)
{
    s_can_rx_queue_reject_count++;
}

void MiniDcm_TaskRuntime_RecordDcmNotificationWakeup(void)
{
    if (MiniDcm_Osal_IsInitialized() == FALSE)
    {
        MiniDcm_Error_Report(MINIDCM_E_NOT_INIT);
        return;
    }

    MiniDcm_Osal_EnterCritical();
    s_dcm_notification_wakeup_count++;
    MiniDcm_Osal_ExitCritical();
}

MiniDcm_TaskRuntimeStatsType MiniDcm_TaskRuntime_GetStats(void)
{
    MiniDcm_TaskRuntimeStatsType stats;
    uint32_t now_ms = 0u;
    uint32_t dcm_last_run_ms;
    uint32_t monitor_last_run_ms;
    uint32_t heartbeat_last_run_ms;
    uint8_t seen_mask;

    if (MiniDcm_Osal_IsInitialized() != FALSE)
    {
        now_ms = MiniDcm_Osal_GetTickMs();
        MiniDcm_Osal_EnterCritical();
    }

    stats.dcm_task_count = s_dcm_task_count;
    stats.monitor_task_count = s_monitor_task_count;
    stats.heartbeat_task_count = s_heartbeat_task_count;
    stats.dcm_stack_high_water_mark = s_dcm_stack_high_water_mark;
    stats.monitor_stack_high_water_mark = s_monitor_stack_high_water_mark;
    stats.heartbeat_stack_high_water_mark = s_heartbeat_stack_high_water_mark;
    dcm_last_run_ms = s_dcm_task_last_run_ms;
    monitor_last_run_ms = s_monitor_task_last_run_ms;
    heartbeat_last_run_ms = s_heartbeat_task_last_run_ms;
    stats.can_rx_notification_count = s_can_rx_notification_count;
    stats.dcm_notification_wakeup_count = s_dcm_notification_wakeup_count;
    stats.can_rx_queue_push_count = s_can_rx_queue_push_count;
    stats.can_rx_queue_reject_count = s_can_rx_queue_reject_count;
    stats.can_rx_queue_last_pending_count = s_can_rx_queue_last_pending_count;
    stats.can_rx_queue_high_water_mark = s_can_rx_queue_high_water_mark;
    seen_mask = s_task_seen_mask;

    if (MiniDcm_Osal_IsInitialized() != FALSE)
    {
        MiniDcm_Osal_ExitCritical();
    }

    stats.dcm_task_last_run_age_ms = MiniDcm_TaskRuntime_GetAgeMs(
        now_ms, dcm_last_run_ms, (uint8_t)(seen_mask & MINIDCM_TASK_ALIVE_DCM));
    stats.monitor_task_last_run_age_ms = MiniDcm_TaskRuntime_GetAgeMs(
        now_ms, monitor_last_run_ms, (uint8_t)(seen_mask & MINIDCM_TASK_ALIVE_MONITOR));
    stats.heartbeat_task_last_run_age_ms = MiniDcm_TaskRuntime_GetAgeMs(
        now_ms, heartbeat_last_run_ms, (uint8_t)(seen_mask & MINIDCM_TASK_ALIVE_HEARTBEAT));
    stats.task_alive_mask = MiniDcm_TaskRuntime_GetAliveMask(
        now_ms, dcm_last_run_ms, monitor_last_run_ms, heartbeat_last_run_ms, seen_mask);
    stats.task_fault_mask = (uint8_t)(MINIDCM_TASK_ALIVE_ALL & (uint8_t)(~stats.task_alive_mask));

    return stats;
}
