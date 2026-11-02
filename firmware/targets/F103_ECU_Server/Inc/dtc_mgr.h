#ifndef DTC_MGR_H
#define DTC_MGR_H

#include "minidcm_error.h"
#include "minidcm_types.h"

#define DTC_MGR_MAX_DTC_COUNT                 4u

#define DTC_MGR_STATUS_TEST_FAILED            ((uint8_t)0x01u)
#define DTC_MGR_STATUS_CONFIRMED              ((uint8_t)0x08u)
#define DTC_MGR_STATUS_AVAILABILITY_MASK      ((uint8_t)(DTC_MGR_STATUS_TEST_FAILED | DTC_MGR_STATUS_CONFIRMED))

#define DTC_MGR_DTC_NONE                      ((uint32_t)0x000000u)
#define DTC_MGR_DTC_MINIDCM_INTERNAL_ERROR    ((uint32_t)0xC00100u)
#define DTC_MGR_DTC_CAN_COMMUNICATION_ERROR   ((uint32_t)0xC00200u)
#define DTC_MGR_DTC_SENSOR_COMMUNICATION_ERROR ((uint32_t)0xC00300u)

typedef struct
{
    uint32_t dtc;
    uint8_t status;
    uint8_t occurrence_count;
} DtcMgr_RecordType;

void DtcMgr_Init(void);
void DtcMgr_ReportError(MiniDcm_ErrorCodeType error);
void DtcMgr_ClearAll(void);
MiniDcm_BoolType DtcMgr_ClearByGroup(uint32_t group_of_dtc);
uint8_t DtcMgr_GetStatusAvailabilityMask(void);
uint8_t DtcMgr_GetActiveCount(void);
uint8_t DtcMgr_GetCountByStatusMask(uint8_t status_mask);
MiniDcm_BoolType DtcMgr_GetFirstActive(DtcMgr_RecordType *record);
MiniDcm_BoolType DtcMgr_GetFirstByStatusMask(uint8_t status_mask, DtcMgr_RecordType *record);

#endif
