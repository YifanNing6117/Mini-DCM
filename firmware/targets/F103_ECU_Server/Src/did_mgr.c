#include "did_mgr.h"
#include "did_cfg.h"
#include "cantp.h"
#include "dtc_mgr.h"
#include "minidcm_build_info.h"
#include "minidcm_error.h"
#include "minidcm_event_log.h"
#include "minidcm_runtime_stats.h"
#include "minidcm_task_runtime.h"
#include "session_mgr.h"
#include "sensor_app.h"

static uint8_t s_did_f187_value;
static const uint8_t s_did_f192_value[] = "MINIDCM-CANTP-MF-V2.9";
static uint8_t s_did_f193_value[16];
static uint8_t s_did_f1a6_value[24];

static void DidMgr_WriteU32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)((value >> 24) & 0xFFu);
    data[1] = (uint8_t)((value >> 16) & 0xFFu);
    data[2] = (uint8_t)((value >> 8) & 0xFFu);
    data[3] = (uint8_t)(value & 0xFFu);
}

static void DidMgr_WriteS16(uint8_t *data, int16_t value)
{
    uint16_t raw_value = (uint16_t)value;

    data[0] = (uint8_t)((raw_value >> 8) & 0xFFu);
    data[1] = (uint8_t)(raw_value & 0xFFu);
}

uint8_t DidMgr_Read(uint16_t did, uint8_t *data, uint8_t *data_len, uint8_t max_len)
{
    const DidCfg_EntryType *cfg;

    if ((data == 0) || (data_len == 0))
    {
        return DID_MGR_BUFFER_TOO_SMALL;
    }

    cfg = DidCfg_Find(did);
    if (cfg == 0)
    {
        return DID_MGR_NOT_FOUND;
    }
    if (cfg->readable == 0u)
    {
        return DID_MGR_NOT_FOUND;
    }
    if (max_len < cfg->data_len)
    {
        return DID_MGR_BUFFER_TOO_SMALL;
    }

    if (did == 0xF190u)
    {
        data[0] = 'F';
        data[1] = '1';
        data[2] = '0';
        data[3] = '3';
        *data_len = 4u;
        return DID_MGR_OK;
    }

    if (did == 0xF192u)
    {
        uint8_t index;

        for (index = 0u; index < (uint8_t)sizeof(s_did_f192_value) - 1u; index++)
        {
            data[index] = s_did_f192_value[index];
        }
        *data_len = (uint8_t)(sizeof(s_did_f192_value) - 1u);
        return DID_MGR_OK;
    }

    if (did == 0xF193u)
    {
        uint8_t index;

        for (index = 0u; index < (uint8_t)sizeof(s_did_f193_value); index++)
        {
            data[index] = s_did_f193_value[index];
        }
        *data_len = (uint8_t)sizeof(s_did_f193_value);
        return DID_MGR_OK;
    }

    if (did == 0xF1A6u)
    {
        uint8_t index;

        for (index = 0u; index < (uint8_t)sizeof(s_did_f1a6_value); index++)
        {
            data[index] = s_did_f1a6_value[index];
        }
        *data_len = (uint8_t)sizeof(s_did_f1a6_value);
        return DID_MGR_OK;
    }

    if (did == 0xF186u)
    {
        data[0] = SessionMgr_GetSession();
        *data_len = 1u;
        return DID_MGR_OK;
    }

    if (did == 0xF187u)
    {
        data[0] = s_did_f187_value;
        *data_len = 1u;
        return DID_MGR_OK;
    }

    if (did == 0xF188u)
    {
        MiniDcm_VersionType version = MiniDcm_BuildInfo_GetFrameworkVersion();

        data[0] = version.major;
        data[1] = version.minor;
        data[2] = version.patch;
        *data_len = 3u;
        return DID_MGR_OK;
    }

    if (did == 0xF189u)
    {
        data[0] = MiniDcm_BuildInfo_GetNodeRole();
        *data_len = 1u;
        return DID_MGR_OK;
    }

    if (did == 0xF18Au)
    {
        MiniDcm_RuntimeStatsType stats = MiniDcm_RuntimeStats_Get();

        DidMgr_WriteU32(data, stats.ecu_rx_count);
        *data_len = 4u;
        return DID_MGR_OK;
    }

    if (did == 0xF18Bu)
    {
        MiniDcm_RuntimeStatsType stats = MiniDcm_RuntimeStats_Get();

        DidMgr_WriteU32(data, stats.ecu_accepted_request_count);
        *data_len = 4u;
        return DID_MGR_OK;
    }

    if (did == 0xF18Cu)
    {
        MiniDcm_ErrorCodeType last_error = MiniDcm_Error_GetLast();

        data[0] = (uint8_t)((last_error >> 8) & 0xFFu);
        data[1] = (uint8_t)(last_error & 0xFFu);
        *data_len = 2u;
        return DID_MGR_OK;
    }

    if (did == 0xF18Du)
    {
        MiniDcm_EventLogEntryType latest_event;

        if (MiniDcm_EventLog_GetLatest(&latest_event) == MINIDCM_TRUE)
        {
            data[0] = latest_event.event_id;
            data[1] = (uint8_t)((latest_event.detail >> 8) & 0xFFu);
            data[2] = (uint8_t)(latest_event.detail & 0xFFu);
            data[3] = (uint8_t)(latest_event.sequence & 0xFFu);
        }
        else
        {
            data[0] = MINIDCM_EVENT_NONE;
            data[1] = 0u;
            data[2] = 0u;
            data[3] = 0u;
        }
        *data_len = 4u;
        return DID_MGR_OK;
    }

    if (did == 0xF18Eu)
    {
        data[0] = DtcMgr_GetActiveCount();
        *data_len = 1u;
        return DID_MGR_OK;
    }

    if (did == 0xF18Fu)
    {
        DtcMgr_RecordType record;

        if (DtcMgr_GetFirstActive(&record) == MINIDCM_TRUE)
        {
            data[0] = (uint8_t)((record.dtc >> 16) & 0xFFu);
            data[1] = (uint8_t)((record.dtc >> 8) & 0xFFu);
            data[2] = (uint8_t)(record.dtc & 0xFFu);
            data[3] = record.status;
        }
        else
        {
            data[0] = 0u;
            data[1] = 0u;
            data[2] = 0u;
            data[3] = 0u;
        }
        *data_len = 4u;
        return DID_MGR_OK;
    }

    if (did == 0xF194u)
    {
        MiniDcm_TaskRuntimeStatsType stats = MiniDcm_TaskRuntime_GetStats();

        DidMgr_WriteU32(data, stats.dcm_task_count);
        *data_len = 4u;
        return DID_MGR_OK;
    }

    if (did == 0xF195u)
    {
        MiniDcm_TaskRuntimeStatsType stats = MiniDcm_TaskRuntime_GetStats();

        DidMgr_WriteU32(data, stats.monitor_task_count);
        *data_len = 4u;
        return DID_MGR_OK;
    }

    if (did == 0xF196u)
    {
        MiniDcm_TaskRuntimeStatsType stats = MiniDcm_TaskRuntime_GetStats();

        DidMgr_WriteU32(data, stats.heartbeat_task_count);
        *data_len = 4u;
        return DID_MGR_OK;
    }

    if (did == 0xF197u)
    {
        MiniDcm_TaskRuntimeStatsType stats = MiniDcm_TaskRuntime_GetStats();

        DidMgr_WriteU32(data, stats.dcm_stack_high_water_mark);
        *data_len = 4u;
        return DID_MGR_OK;
    }

    if (did == 0xF198u)
    {
        MiniDcm_TaskRuntimeStatsType stats = MiniDcm_TaskRuntime_GetStats();

        DidMgr_WriteU32(data, stats.monitor_stack_high_water_mark);
        *data_len = 4u;
        return DID_MGR_OK;
    }

    if (did == 0xF199u)
    {
        MiniDcm_TaskRuntimeStatsType stats = MiniDcm_TaskRuntime_GetStats();

        DidMgr_WriteU32(data, stats.heartbeat_stack_high_water_mark);
        *data_len = 4u;
        return DID_MGR_OK;
    }

    if (did == 0xF19Au)
    {
        MiniDcm_TaskRuntimeStatsType stats = MiniDcm_TaskRuntime_GetStats();

        data[0] = stats.task_alive_mask;
        *data_len = 1u;
        return DID_MGR_OK;
    }

    if (did == 0xF19Bu)
    {
        MiniDcm_TaskRuntimeStatsType stats = MiniDcm_TaskRuntime_GetStats();

        data[0] = stats.task_fault_mask;
        *data_len = 1u;
        return DID_MGR_OK;
    }

    if (did == 0xF19Cu)
    {
        MiniDcm_TaskRuntimeStatsType stats = MiniDcm_TaskRuntime_GetStats();

        DidMgr_WriteU32(data, stats.dcm_task_last_run_age_ms);
        *data_len = 4u;
        return DID_MGR_OK;
    }

    if (did == 0xF19Du)
    {
        MiniDcm_TaskRuntimeStatsType stats = MiniDcm_TaskRuntime_GetStats();

        DidMgr_WriteU32(data, stats.monitor_task_last_run_age_ms);
        *data_len = 4u;
        return DID_MGR_OK;
    }

    if (did == 0xF19Eu)
    {
        MiniDcm_TaskRuntimeStatsType stats = MiniDcm_TaskRuntime_GetStats();

        DidMgr_WriteU32(data, stats.heartbeat_task_last_run_age_ms);
        *data_len = 4u;
        return DID_MGR_OK;
    }

    if (did == 0xF1A0u)
    {
        MiniDcm_TaskRuntimeStatsType stats = MiniDcm_TaskRuntime_GetStats();

        DidMgr_WriteU32(data, stats.can_rx_notification_count);
        *data_len = 4u;
        return DID_MGR_OK;
    }

    if (did == 0xF1A1u)
    {
        MiniDcm_TaskRuntimeStatsType stats = MiniDcm_TaskRuntime_GetStats();

        DidMgr_WriteU32(data, stats.dcm_notification_wakeup_count);
        *data_len = 4u;
        return DID_MGR_OK;
    }

    if (did == 0xF1A2u)
    {
        MiniDcm_TaskRuntimeStatsType stats = MiniDcm_TaskRuntime_GetStats();

        DidMgr_WriteU32(data, stats.can_rx_queue_push_count);
        *data_len = 4u;
        return DID_MGR_OK;
    }

    if (did == 0xF1A3u)
    {
        MiniDcm_TaskRuntimeStatsType stats = MiniDcm_TaskRuntime_GetStats();

        DidMgr_WriteU32(data, stats.can_rx_queue_last_pending_count);
        *data_len = 4u;
        return DID_MGR_OK;
    }

    if (did == 0xF1A4u)
    {
        MiniDcm_TaskRuntimeStatsType stats = MiniDcm_TaskRuntime_GetStats();

        DidMgr_WriteU32(data, stats.can_rx_queue_high_water_mark);
        *data_len = 4u;
        return DID_MGR_OK;
    }

    if (did == 0xF1A5u)
    {
        MiniDcm_TaskRuntimeStatsType stats = MiniDcm_TaskRuntime_GetStats();

        DidMgr_WriteU32(data, stats.can_rx_queue_reject_count);
        *data_len = 4u;
        return DID_MGR_OK;
    }

    if (did == 0xF1B0u)
    {
        CanTp_RuntimeStatsType stats = CanTp_GetRuntimeStats();

        DidMgr_WriteU32(data, stats.tx_multi_frame_start_count);
        *data_len = 4u;
        return DID_MGR_OK;
    }

    if (did == 0xF1B1u)
    {
        CanTp_RuntimeStatsType stats = CanTp_GetRuntimeStats();

        DidMgr_WriteU32(data, stats.rx_multi_frame_start_count);
        *data_len = 4u;
        return DID_MGR_OK;
    }

    if (did == 0xF1B2u)
    {
        CanTp_RuntimeStatsType stats = CanTp_GetRuntimeStats();

        DidMgr_WriteU32(data, stats.tx_multi_frame_complete_count);
        *data_len = 4u;
        return DID_MGR_OK;
    }

    if (did == 0xF1B3u)
    {
        CanTp_RuntimeStatsType stats = CanTp_GetRuntimeStats();

        DidMgr_WriteU32(data, stats.rx_multi_frame_complete_count);
        *data_len = 4u;
        return DID_MGR_OK;
    }

    if (did == 0xF1B4u)
    {
        CanTp_RuntimeStatsType stats = CanTp_GetRuntimeStats();

        DidMgr_WriteU32(data, stats.flow_control_tx_count);
        *data_len = 4u;
        return DID_MGR_OK;
    }

    if (did == 0xF1B5u)
    {
        CanTp_RuntimeStatsType stats = CanTp_GetRuntimeStats();

        DidMgr_WriteU32(data, stats.flow_control_rx_count);
        *data_len = 4u;
        return DID_MGR_OK;
    }

    if (did == 0xF1B6u)
    {
        CanTp_RuntimeStatsType stats = CanTp_GetRuntimeStats();

        DidMgr_WriteU32(data, stats.error_count);
        *data_len = 4u;
        return DID_MGR_OK;
    }

    if (did == 0xF1B7u)
    {
        CanTp_RuntimeStatsType stats = CanTp_GetRuntimeStats();

        data[0] = stats.state_flags;
        data[1] = stats.last_error;
        *data_len = 2u;
        return DID_MGR_OK;
    }

    if ((did >= 0xF1C0u) && (did <= 0xF1C6u))
    {
        SensorApp_SnapshotType snapshot;

        SensorApp_GetSnapshot(&snapshot);
        if (did == 0xF1C0u)
        {
            data[0] = snapshot.source;
            data[1] = snapshot.health;
            *data_len = 2u;
        }
        else if (did == 0xF1C1u)
        {
            DidMgr_WriteS16(data, snapshot.acceleration_x_mg);
            *data_len = 2u;
        }
        else if (did == 0xF1C2u)
        {
            DidMgr_WriteS16(data, snapshot.acceleration_y_mg);
            *data_len = 2u;
        }
        else if (did == 0xF1C3u)
        {
            DidMgr_WriteS16(data, snapshot.acceleration_z_mg);
            *data_len = 2u;
        }
        else if (did == 0xF1C4u)
        {
            DidMgr_WriteS16(data, snapshot.temperature_deci_c);
            *data_len = 2u;
        }
        else if (did == 0xF1C5u)
        {
            DidMgr_WriteU32(data, snapshot.sample_count);
            *data_len = 4u;
        }
        else
        {
            data[0] = snapshot.fault_flags;
            *data_len = 1u;
        }
        return DID_MGR_OK;
    }

    return DID_MGR_NOT_FOUND;
}

uint8_t DidMgr_Write(uint16_t did, const uint8_t *data, uint8_t data_len)
{
    const DidCfg_EntryType *cfg;

    if (data == 0)
    {
        return DID_MGR_INVALID_LENGTH;
    }

    cfg = DidCfg_Find(did);
    if (cfg == 0)
    {
        return DID_MGR_NOT_FOUND;
    }
    if (cfg->writable == 0u)
    {
        return DID_MGR_READ_ONLY;
    }
    if (data_len != cfg->data_len)
    {
        return DID_MGR_INVALID_LENGTH;
    }

    if (did == 0xF187u)
    {
        s_did_f187_value = data[0];
        return DID_MGR_OK;
    }

    if (did == 0xF193u)
    {
        uint8_t index;

        for (index = 0u; index < (uint8_t)sizeof(s_did_f193_value); index++)
        {
            s_did_f193_value[index] = data[index];
        }
        return DID_MGR_OK;
    }

    if (did == 0xF1A6u)
    {
        uint8_t index;

        for (index = 0u; index < (uint8_t)sizeof(s_did_f1a6_value); index++)
        {
            s_did_f1a6_value[index] = data[index];
        }
        return DID_MGR_OK;
    }

    return DID_MGR_NOT_FOUND;
}
