#include "ecu_app.h"

#include "can_if.h"
#include "cantp.h"
#include "minidcm_error.h"
#include "minidcm_event_log.h"
#include "minidcm_runtime_stats.h"
#include "session_mgr.h"
#include "uds_server.h"

static uint32_t s_ecu_app_rx_count;

static void EcuApp_ProcessUdsPayload(const uint8_t *uds_payload, uint8_t uds_payload_len, uint32_t now_ms);

static void EcuApp_ReportCanTpError(CanTp_StatusType status)
{
    if (status == CANTP_OK)
    {
        return;
    }
    if (status == CANTP_ERR_TIMEOUT)
    {
        MiniDcm_Error_Report(MINIDCM_E_TIMEOUT);
        return;
    }
    MiniDcm_Error_Report(MINIDCM_E_CANTP_FORMAT);
}

static void EcuApp_SendRawEchoResponse(const CanIf_FrameType *rx_frame)
{
    uint8_t tx_data[CANIF_CAN_DATA_LEN] = {0};
    uint8_t dlc = rx_frame->dlc;

    if (dlc > CANIF_CAN_DATA_LEN)
    {
        dlc = CANIF_CAN_DATA_LEN;
    }

    tx_data[0] = 0xECu;
    tx_data[1] = 'U';
    tx_data[2] = dlc;
    tx_data[3] = rx_frame->data[0];
    tx_data[4] = rx_frame->data[1];
    tx_data[5] = rx_frame->data[2];
    tx_data[6] = rx_frame->data[3];
    tx_data[7] = (uint8_t)(s_ecu_app_rx_count & 0xFFu);

    (void)CanIf_Transmit(CANIF_ECU_RESPONSE_ID, tx_data, CANIF_CAN_DATA_LEN);
}

static void EcuApp_ProcessSingleFrameRequest(const CanIf_FrameType *rx_frame, uint32_t now_ms)
{
    CanTp_FrameViewType tp_frame = {0};

    if (CanTp_DecodeSingleFrame(rx_frame->data, rx_frame->dlc, &tp_frame) == CANTP_SF_OK)
    {
        EcuApp_ProcessUdsPayload(tp_frame.payload, tp_frame.payload_len, now_ms);
        return;
    }

    EcuApp_SendRawEchoResponse(rx_frame);
}

static void EcuApp_ProcessUdsPayload(const uint8_t *uds_payload, uint8_t uds_payload_len, uint32_t now_ms)
{
    uint8_t uds_resp[UDS_SERVER_MAX_RESPONSE_LEN] = {0};
    uint8_t uds_resp_len = 0u;

    if ((uds_payload == 0) || (uds_payload_len == 0u))
    {
        return;
    }

    MiniDcm_EventLog_RecordWithTimestamp(
        MINIDCM_EVENT_UDS_REQUEST,
        (uint16_t)(((uint16_t)uds_payload[0] << 8) | uds_payload_len),
        now_ms);
    SessionMgr_OnUdsActivity(now_ms);
    UdsServer_Dispatch(uds_payload, uds_payload_len, now_ms, uds_resp, &uds_resp_len, sizeof(uds_resp));
    if (uds_resp_len > 0u)
    {
        (void)CanTp_Transmit(CANIF_ECU_RESPONSE_ID, uds_resp, uds_resp_len, now_ms);
    }
}

void EcuApp_MainFunction(uint32_t now_ms)
{
    CanIf_FrameType rx_frame = {0};
    CanTp_PciType pci_type;
    uint8_t cantp_pdu[CANTP_MAX_PDU_LEN] = {0};
    uint16_t cantp_pdu_len = 0u;

    SessionMgr_MainFunction(now_ms);

    while (CanIf_Receive(&rx_frame) == CANIF_OK)
    {
        MiniDcm_RuntimeStats_IncEcuRxCount();

        if ((rx_frame.id == CANIF_ECU_REQUEST_ID) || (rx_frame.id == CANIF_FUNCTIONAL_REQUEST_ID))
        {
            s_ecu_app_rx_count++;
            if ((CanTp_GetPciType(rx_frame.data, rx_frame.dlc, &pci_type) == CANTP_OK) &&
                (pci_type == CANTP_PCI_FLOW_CONTROL))
            {
                (void)CanTp_HandleFlowControl(rx_frame.data, rx_frame.dlc, now_ms);
            }
            else if ((CanTp_GetPciType(rx_frame.data, rx_frame.dlc, &pci_type) == CANTP_OK) &&
                     ((pci_type == CANTP_PCI_FIRST_FRAME) || (pci_type == CANTP_PCI_CONSECUTIVE_FRAME)))
            {
                (void)CanTp_RxIndication(rx_frame.data, rx_frame.dlc, CANIF_ECU_RESPONSE_ID, now_ms);
                if ((CanTp_IsRxPduAvailable() != 0u) &&
                    (CanTp_GetRxPdu(cantp_pdu, sizeof(cantp_pdu), &cantp_pdu_len) == CANTP_OK) &&
                    (cantp_pdu_len <= 0xFFu))
                {
                    MiniDcm_RuntimeStats_IncEcuAcceptedRequestCount();
                    EcuApp_ProcessUdsPayload(cantp_pdu, (uint8_t)cantp_pdu_len, now_ms);
                }
            }
            else
            {
                MiniDcm_RuntimeStats_IncEcuAcceptedRequestCount();
                EcuApp_ProcessSingleFrameRequest(&rx_frame, now_ms);
            }
        }
        else
        {
            MiniDcm_RuntimeStats_IncEcuIgnoredFrameCount();
        }
    }

    CanTp_MainFunction(now_ms);
    EcuApp_ReportCanTpError(CanTp_GetAndClearError());
    /* Drain ECU responses whenever bxCAN mailboxes become available. */
    CanIf_MainFunction();
}

uint32_t EcuApp_GetRxCount(void)
{
    return s_ecu_app_rx_count;
}
