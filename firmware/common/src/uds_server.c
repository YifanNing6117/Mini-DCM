#include "uds_server.h"
#include "did_mgr.h"
#include "dtc_mgr.h"
#include "minidcm_error.h"
#include "security_mgr.h"
#include "session_mgr.h"
#include "sensor_app.h"
#include "uds_cfg.h"
#include "uds_nrc.h"

#define UDS_ROUTINE_CTRL_START_ROUTINE        ((uint8_t)0x01u)
#define UDS_ROUTINE_ID_TRIGGER_CAN_DTC        ((uint16_t)0x0201u)
#define UDS_ROUTINE_ID_CLEAR_DEMO_DTC         ((uint16_t)0x0202u)
#define UDS_ROUTINE_ID_INJECT_SENSOR_FAULT    ((uint16_t)0x0203u)
#define UDS_ROUTINE_ID_RESTORE_SENSOR_NORMAL  ((uint16_t)0x0204u)
#define UDS_READ_DTC_REPORT_NUMBER_BY_MASK    ((uint8_t)0x01u)
#define UDS_READ_DTC_REPORT_DTC_BY_MASK       ((uint8_t)0x02u)

static void UdsServer_BuildNegativeResponse(uint8_t sid, uint8_t nrc, uint8_t *response, uint8_t *response_len, uint8_t response_max_len)
{
    if ((response == 0) || (response_len == 0) || (response_max_len < 3u))
    {
        return;
    }
    response[0] = 0x7Fu;
    response[1] = sid;
    response[2] = nrc;
    *response_len = 3u;
}

void UdsServer_HandleReadDataByIdentifier(const uint8_t *request, uint8_t request_len, uint32_t now_ms, uint8_t *response, uint8_t *response_len, uint8_t response_max_len)
{
    uint16_t did;
    uint8_t did_data_len = 0u;
    uint8_t did_status;
    (void)now_ms;

    if (request_len < 3u)
    {
        UdsServer_BuildNegativeResponse(0x22u, UDS_NRC_INCORRECT_MESSAGE_LENGTH, response, response_len, response_max_len);
        return;
    }
    if (response_max_len < 3u)
    {
        return;
    }

    did = ((uint16_t)request[1] << 8) | request[2];
    response[0] = 0x62u;
    response[1] = request[1];
    response[2] = request[2];

    did_status = DidMgr_Read(did, &response[3], &did_data_len, (uint8_t)(response_max_len - 3u));
    if (did_status == DID_MGR_OK)
    {
        *response_len = (uint8_t)(3u + did_data_len);
        return;
    }

    UdsServer_BuildNegativeResponse(0x22u, UDS_NRC_REQUEST_OUT_OF_RANGE, response, response_len, response_max_len);
}

void UdsServer_HandleDiagnosticSessionControl(const uint8_t *request, uint8_t request_len, uint32_t now_ms, uint8_t *response, uint8_t *response_len, uint8_t response_max_len)
{
    uint8_t session;

    if (request_len != 2u)
    {
        UdsServer_BuildNegativeResponse(0x10u, UDS_NRC_INCORRECT_MESSAGE_LENGTH, response, response_len, response_max_len);
        return;
    }
    if (response_max_len < 6u)
    {
        return;
    }

    session = (uint8_t)(request[1] & 0x7Fu);
    if (SessionMgr_SetSession(session, now_ms) != SESSION_MGR_OK)
    {
        UdsServer_BuildNegativeResponse(0x10u, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED, response, response_len, response_max_len);
        return;
    }

    response[0] = 0x50u;
    response[1] = session;
    response[2] = 0x00u;
    response[3] = 0x32u;
    response[4] = 0x01u;
    response[5] = 0xF4u;
    *response_len = 6u;
}

void UdsServer_HandleClearDiagnosticInformation(const uint8_t *request, uint8_t request_len, uint32_t now_ms, uint8_t *response, uint8_t *response_len, uint8_t response_max_len)
{
    uint32_t group_of_dtc;
    (void)now_ms;

    if (request_len != 4u)
    {
        UdsServer_BuildNegativeResponse(0x14u, UDS_NRC_INCORRECT_MESSAGE_LENGTH, response, response_len, response_max_len);
        return;
    }
    if (response_max_len < 1u)
    {
        return;
    }

    group_of_dtc = ((uint32_t)request[1] << 16) | ((uint32_t)request[2] << 8) | request[3];
    if (DtcMgr_ClearByGroup(group_of_dtc) == MINIDCM_FALSE)
    {
        UdsServer_BuildNegativeResponse(0x14u, UDS_NRC_REQUEST_OUT_OF_RANGE, response, response_len, response_max_len);
        return;
    }

    if (group_of_dtc == 0xFFFFFFu)
    {
        MiniDcm_Error_Clear();
    }

    response[0] = 0x54u;
    *response_len = 1u;
}

void UdsServer_HandleReadDtcInformation(const uint8_t *request, uint8_t request_len, uint32_t now_ms, uint8_t *response, uint8_t *response_len, uint8_t response_max_len)
{
    uint8_t sub_function;
    uint8_t status_mask;
    uint8_t dtc_count;
    DtcMgr_RecordType record;
    (void)now_ms;

    if (request_len != 3u)
    {
        UdsServer_BuildNegativeResponse(0x19u, UDS_NRC_INCORRECT_MESSAGE_LENGTH, response, response_len, response_max_len);
        return;
    }

    sub_function = (uint8_t)(request[1] & 0x7Fu);
    status_mask = request[2];

    if (sub_function == UDS_READ_DTC_REPORT_NUMBER_BY_MASK)
    {
        if (response_max_len < 6u)
        {
            return;
        }

        dtc_count = DtcMgr_GetCountByStatusMask(status_mask);
        response[0] = 0x59u;
        response[1] = UDS_READ_DTC_REPORT_NUMBER_BY_MASK;
        response[2] = DtcMgr_GetStatusAvailabilityMask();
        response[3] = 0x01u;
        response[4] = 0x00u;
        response[5] = dtc_count;
        *response_len = 6u;
        return;
    }

    if (sub_function == UDS_READ_DTC_REPORT_DTC_BY_MASK)
    {
        if (response_max_len < 3u)
        {
            return;
        }

        response[0] = 0x59u;
        response[1] = UDS_READ_DTC_REPORT_DTC_BY_MASK;
        response[2] = DtcMgr_GetStatusAvailabilityMask();
        *response_len = 3u;

        if (DtcMgr_GetFirstByStatusMask(status_mask, &record) == MINIDCM_TRUE)
        {
            if (response_max_len < 7u)
            {
                return;
            }
            response[3] = (uint8_t)((record.dtc >> 16) & 0xFFu);
            response[4] = (uint8_t)((record.dtc >> 8) & 0xFFu);
            response[5] = (uint8_t)(record.dtc & 0xFFu);
            response[6] = record.status;
            *response_len = 7u;
        }
        return;
    }

    UdsServer_BuildNegativeResponse(0x19u, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED, response, response_len, response_max_len);
}

void UdsServer_HandleSecurityAccess(const uint8_t *request, uint8_t request_len, uint32_t now_ms, uint8_t *response, uint8_t *response_len, uint8_t response_max_len)
{
    uint8_t sub_function;
    SecurityMgr_StatusType status;

    if (request_len < 2u)
    {
        UdsServer_BuildNegativeResponse(0x27u, UDS_NRC_INCORRECT_MESSAGE_LENGTH, response, response_len, response_max_len);
        return;
    }
    if (response_max_len < 6u)
    {
        return;
    }

    sub_function = (uint8_t)(request[1] & 0x7Fu);
    if (sub_function == 0x01u)
    {
        response[0] = 0x67u;
        response[1] = 0x01u;
        status = SecurityMgr_RequestSeed(now_ms, &response[2], SECURITY_MGR_SEED_LEN);
        if (status == SECURITY_MGR_OK)
        {
            *response_len = 6u;
            return;
        }
        UdsServer_BuildNegativeResponse(0x27u, UDS_NRC_EXCEED_NUMBER_OF_ATTEMPTS, response, response_len, response_max_len);
        return;
    }

    if (sub_function == 0x02u)
    {
        if (request_len != 6u)
        {
            UdsServer_BuildNegativeResponse(0x27u, UDS_NRC_INCORRECT_MESSAGE_LENGTH, response, response_len, response_max_len);
            return;
        }

        status = SecurityMgr_ValidateKey(now_ms, &request[2], SECURITY_MGR_KEY_LEN);
        if (status == SECURITY_MGR_OK)
        {
            response[0] = 0x67u;
            response[1] = 0x02u;
            *response_len = 2u;
            return;
        }
        if (status == SECURITY_MGR_ERR_LOCKED)
        {
            UdsServer_BuildNegativeResponse(0x27u, UDS_NRC_EXCEED_NUMBER_OF_ATTEMPTS, response, response_len, response_max_len);
            return;
        }
        UdsServer_BuildNegativeResponse(0x27u, UDS_NRC_INVALID_KEY, response, response_len, response_max_len);
        return;
    }

    UdsServer_BuildNegativeResponse(0x27u, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED, response, response_len, response_max_len);
}

void UdsServer_HandleWriteDataByIdentifier(const uint8_t *request, uint8_t request_len, uint32_t now_ms, uint8_t *response, uint8_t *response_len, uint8_t response_max_len)
{
    uint16_t did;
    uint8_t did_status;
    (void)now_ms;

    if (request_len < 4u)
    {
        UdsServer_BuildNegativeResponse(0x2Eu, UDS_NRC_INCORRECT_MESSAGE_LENGTH, response, response_len, response_max_len);
        return;
    }
    if (response_max_len < 3u)
    {
        return;
    }
    if (SecurityMgr_IsUnlocked() == 0u)
    {
        UdsServer_BuildNegativeResponse(0x2Eu, UDS_NRC_SECURITY_ACCESS_DENIED, response, response_len, response_max_len);
        return;
    }

    did = ((uint16_t)request[1] << 8) | request[2];
    did_status = DidMgr_Write(did, &request[3], (uint8_t)(request_len - 3u));
    if (did_status == DID_MGR_OK)
    {
        response[0] = 0x6Eu;
        response[1] = request[1];
        response[2] = request[2];
        *response_len = 3u;
        return;
    }

    if (did_status == DID_MGR_INVALID_LENGTH)
    {
        UdsServer_BuildNegativeResponse(0x2Eu, UDS_NRC_INCORRECT_MESSAGE_LENGTH, response, response_len, response_max_len);
        return;
    }

    UdsServer_BuildNegativeResponse(0x2Eu, UDS_NRC_REQUEST_OUT_OF_RANGE, response, response_len, response_max_len);
}

void UdsServer_HandleRoutineControl(const uint8_t *request, uint8_t request_len, uint32_t now_ms, uint8_t *response, uint8_t *response_len, uint8_t response_max_len)
{
    uint8_t routine_control_type;
    uint16_t routine_id;
    (void)now_ms;

    if (request_len != 4u)
    {
        UdsServer_BuildNegativeResponse(0x31u, UDS_NRC_INCORRECT_MESSAGE_LENGTH, response, response_len, response_max_len);
        return;
    }
    if (response_max_len < 4u)
    {
        return;
    }

    routine_control_type = (uint8_t)(request[1] & 0x7Fu);
    if (routine_control_type != UDS_ROUTINE_CTRL_START_ROUTINE)
    {
        UdsServer_BuildNegativeResponse(0x31u, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED, response, response_len, response_max_len);
        return;
    }

    routine_id = ((uint16_t)request[2] << 8) | request[3];
    if (routine_id == UDS_ROUTINE_ID_TRIGGER_CAN_DTC)
    {
        MiniDcm_Error_Report(MINIDCM_E_CAN_TX);
    }
    else if (routine_id == UDS_ROUTINE_ID_CLEAR_DEMO_DTC)
    {
        DtcMgr_ClearAll();
        MiniDcm_Error_Clear();
    }
    else if (routine_id == UDS_ROUTINE_ID_INJECT_SENSOR_FAULT)
    {
        SensorApp_InjectMockCommunicationFault();
    }
    else if (routine_id == UDS_ROUTINE_ID_RESTORE_SENSOR_NORMAL)
    {
        SensorApp_RestoreMockNormal();
    }
    else
    {
        UdsServer_BuildNegativeResponse(0x31u, UDS_NRC_REQUEST_OUT_OF_RANGE, response, response_len, response_max_len);
        return;
    }

    response[0] = 0x71u;
    response[1] = UDS_ROUTINE_CTRL_START_ROUTINE;
    response[2] = request[2];
    response[3] = request[3];
    *response_len = 4u;
}

void UdsServer_HandleTesterPresent(const uint8_t *request, uint8_t request_len, uint32_t now_ms, uint8_t *response, uint8_t *response_len, uint8_t response_max_len)
{
    (void)now_ms;

    if (response_max_len < 2u)
    {
        return;
    }
    response[0] = 0x7Eu;
    response[1] = (request_len >= 2u) ? (uint8_t)(request[1] & 0x7Fu) : 0x00u;
    *response_len = 2u;
}

void UdsServer_Dispatch(const uint8_t *request, uint8_t request_len, uint32_t now_ms, uint8_t *response, uint8_t *response_len, uint8_t response_max_len)
{
    uint8_t sid;
    uint8_t i;
    uint8_t service_count = 0u;
    const UdsCfg_ServiceType *service_table;

    if ((request == 0) || (response == 0) || (response_len == 0))
    {
        return;
    }
    *response_len = 0u;

    if (request_len == 0u)
    {
        UdsServer_BuildNegativeResponse(0x00u, UDS_NRC_INCORRECT_MESSAGE_LENGTH, response, response_len, response_max_len);
        return;
    }

    sid = request[0];
    service_table = UdsCfg_GetServiceTable(&service_count);
    for (i = 0u; i < service_count; i++)
    {
        if (service_table[i].sid == sid)
        {
            service_table[i].handler(request, request_len, now_ms, response, response_len, response_max_len);
            return;
        }
    }

    UdsServer_BuildNegativeResponse(sid, UDS_NRC_SERVICE_NOT_SUPPORTED, response, response_len, response_max_len);
}
