#include "dtc_mgr.h"

static DtcMgr_RecordType s_dtc_records[DTC_MGR_MAX_DTC_COUNT];

static uint32_t DtcMgr_MapErrorToDtc(MiniDcm_ErrorCodeType error)
{
    if ((error == MINIDCM_E_CAN_TX) || (error == MINIDCM_E_CAN_RX))
    {
        return DTC_MGR_DTC_CAN_COMMUNICATION_ERROR;
    }

    if (error == MINIDCM_E_SENSOR_COMMUNICATION)
    {
        return DTC_MGR_DTC_SENSOR_COMMUNICATION_ERROR;
    }

    return DTC_MGR_DTC_NONE;
}

static DtcMgr_RecordType *DtcMgr_FindRecord(uint32_t dtc)
{
    uint8_t index;

    for (index = 0u; index < DTC_MGR_MAX_DTC_COUNT; index++)
    {
        if (s_dtc_records[index].dtc == dtc)
        {
            return &s_dtc_records[index];
        }
    }

    return 0;
}

static DtcMgr_RecordType *DtcMgr_FindFreeRecord(void)
{
    uint8_t index;

    for (index = 0u; index < DTC_MGR_MAX_DTC_COUNT; index++)
    {
        if (s_dtc_records[index].dtc == DTC_MGR_DTC_NONE)
        {
            return &s_dtc_records[index];
        }
    }

    return 0;
}

void DtcMgr_Init(void)
{
    uint8_t index;

    for (index = 0u; index < DTC_MGR_MAX_DTC_COUNT; index++)
    {
        s_dtc_records[index].dtc = DTC_MGR_DTC_NONE;
        s_dtc_records[index].status = 0u;
        s_dtc_records[index].occurrence_count = 0u;
    }
}

void DtcMgr_ReportError(MiniDcm_ErrorCodeType error)
{
    uint32_t dtc;
    DtcMgr_RecordType *record;

    dtc = DtcMgr_MapErrorToDtc(error);
    if (dtc == DTC_MGR_DTC_NONE)
    {
        return;
    }

    record = DtcMgr_FindRecord(dtc);
    if (record == 0)
    {
        record = DtcMgr_FindFreeRecord();
    }
    if (record == 0)
    {
        return;
    }

    record->dtc = dtc;
    record->status = (uint8_t)(DTC_MGR_STATUS_TEST_FAILED | DTC_MGR_STATUS_CONFIRMED);
    if (record->occurrence_count < 0xFFu)
    {
        record->occurrence_count++;
    }
}

void DtcMgr_ClearAll(void)
{
    DtcMgr_Init();
}

MiniDcm_BoolType DtcMgr_ClearByGroup(uint32_t group_of_dtc)
{
    uint8_t index;
    MiniDcm_BoolType cleared = MINIDCM_FALSE;

    if (group_of_dtc == 0xFFFFFFu)
    {
        DtcMgr_ClearAll();
        return MINIDCM_TRUE;
    }

    for (index = 0u; index < DTC_MGR_MAX_DTC_COUNT; index++)
    {
        if (s_dtc_records[index].dtc == group_of_dtc)
        {
            s_dtc_records[index].dtc = DTC_MGR_DTC_NONE;
            s_dtc_records[index].status = 0u;
            s_dtc_records[index].occurrence_count = 0u;
            cleared = MINIDCM_TRUE;
        }
    }

    return cleared;
}

uint8_t DtcMgr_GetStatusAvailabilityMask(void)
{
    return DTC_MGR_STATUS_AVAILABILITY_MASK;
}

uint8_t DtcMgr_GetActiveCount(void)
{
    uint8_t index;
    uint8_t count = 0u;

    for (index = 0u; index < DTC_MGR_MAX_DTC_COUNT; index++)
    {
        if (s_dtc_records[index].dtc != DTC_MGR_DTC_NONE)
        {
            count++;
        }
    }

    return count;
}

uint8_t DtcMgr_GetCountByStatusMask(uint8_t status_mask)
{
    uint8_t index;
    uint8_t count = 0u;

    for (index = 0u; index < DTC_MGR_MAX_DTC_COUNT; index++)
    {
        if ((s_dtc_records[index].dtc != DTC_MGR_DTC_NONE) &&
            ((s_dtc_records[index].status & status_mask) != 0u))
        {
            count++;
        }
    }

    return count;
}

MiniDcm_BoolType DtcMgr_GetFirstActive(DtcMgr_RecordType *record)
{
    uint8_t index;

    if (record == 0)
    {
        return MINIDCM_FALSE;
    }

    for (index = 0u; index < DTC_MGR_MAX_DTC_COUNT; index++)
    {
        if (s_dtc_records[index].dtc != DTC_MGR_DTC_NONE)
        {
            *record = s_dtc_records[index];
            return MINIDCM_TRUE;
        }
    }

    return MINIDCM_FALSE;
}

MiniDcm_BoolType DtcMgr_GetFirstByStatusMask(uint8_t status_mask, DtcMgr_RecordType *record)
{
    uint8_t index;

    if (record == 0)
    {
        return MINIDCM_FALSE;
    }

    for (index = 0u; index < DTC_MGR_MAX_DTC_COUNT; index++)
    {
        if ((s_dtc_records[index].dtc != DTC_MGR_DTC_NONE) &&
            ((s_dtc_records[index].status & status_mask) != 0u))
        {
            *record = s_dtc_records[index];
            return MINIDCM_TRUE;
        }
    }

    return MINIDCM_FALSE;
}
