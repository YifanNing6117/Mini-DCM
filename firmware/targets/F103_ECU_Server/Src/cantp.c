#include "cantp.h"
#include "can_if.h"

#define CANTP_N_BS_TIMEOUT_MS 1000u
#define CANTP_N_CR_TIMEOUT_MS 1000u
#define CANTP_RX_FLOW_CONTROL_BLOCK_SIZE 2u
#define CANTP_RX_FLOW_CONTROL_ST_MIN_MS 10u

typedef enum
{
    CANTP_TX_IDLE = 0u,
    CANTP_TX_WAIT_FC,
    CANTP_TX_SEND_CF
} CanTp_TxStateType;

typedef struct
{
    CanTp_TxStateType state;
    uint32_t can_id;
    uint32_t deadline_ms;
    uint32_t next_cf_ms;
    uint16_t payload_len;
    uint16_t offset;
    uint8_t sequence_number;
    uint8_t block_size;
    uint8_t cf_in_block;
    uint8_t st_min_ms;
    uint8_t payload[CANTP_MAX_PDU_LEN];
} CanTp_TxSessionType;

typedef enum
{
    CANTP_RX_IDLE = 0u,
    CANTP_RX_WAIT_CF
} CanTp_RxStateType;

typedef struct
{
    CanTp_RxStateType state;
    uint32_t deadline_ms;
    uint16_t payload_len;
    uint16_t offset;
    uint8_t expected_sequence_number;
    uint8_t cf_in_block;
    uint8_t pdu_available;
    uint8_t payload[CANTP_MAX_PDU_LEN];
} CanTp_RxSessionType;

static CanTp_TxSessionType s_tx_session;
static CanTp_RxSessionType s_rx_session;
static CanTp_StatusType s_pending_error;
static CanTp_RuntimeStatsType s_runtime_stats;

static uint8_t CanTp_DecodeStMinMs(uint8_t st_min, uint8_t *st_min_ms)
{
    if (st_min_ms == 0)
    {
        return 0u;
    }
    /* The 1 ms ECU scheduler supports ISO-TP's millisecond STmin range.
       The 0xF1..0xF9 100-us encodings need a higher-resolution timer. */
    if (st_min > 0x7Fu)
    {
        return 0u;
    }
    *st_min_ms = st_min;
    return 1u;
}

static void CanTp_SetPendingError(CanTp_StatusType status)
{
    if (status != CANTP_OK)
    {
        s_pending_error = status;
        s_runtime_stats.last_error = (uint8_t)status;
        s_runtime_stats.error_count++;
    }
}

static void CanTp_ClearCanData(uint8_t *can_data)
{
    uint8_t index;

    for (index = 0u; index < CANTP_CAN_DATA_LEN; index++)
    {
        can_data[index] = 0u;
    }
}

CanTpSf_StatusType CanTp_DecodeSingleFrame(const uint8_t *can_data, uint8_t can_dlc, CanTp_FrameViewType *frame)
{
    const uint8_t *payload = 0;
    uint8_t payload_len = 0u;
    CanTpSf_StatusType status;

    if (frame == 0)
    {
        return CANTP_SF_ERR_PARAM;
    }

    frame->payload = 0;
    frame->payload_len = 0u;

    status = CanTpSf_Decode(can_data, can_dlc, &payload, &payload_len);
    if (status == CANTP_SF_OK)
    {
        frame->payload = payload;
        frame->payload_len = payload_len;
    }

    return status;
}

CanTpSf_StatusType CanTp_EncodeSingleFrame(const uint8_t *payload, uint8_t payload_len, uint8_t *can_data)
{
    return CanTpSf_Encode(payload, payload_len, can_data);
}

CanTp_StatusType CanTp_GetPciType(const uint8_t *can_data, uint8_t can_dlc, CanTp_PciType *pci_type)
{
    uint8_t raw_type;

    if ((can_data == 0) || (pci_type == 0))
    {
        return CANTP_ERR_PARAM;
    }
    if (can_dlc == 0u)
    {
        return CANTP_ERR_LENGTH;
    }

    raw_type = (uint8_t)((can_data[0] >> 4) & 0x0Fu);
    if (raw_type > (uint8_t)CANTP_PCI_FLOW_CONTROL)
    {
        return CANTP_ERR_FRAME_TYPE;
    }

    *pci_type = (CanTp_PciType)raw_type;
    return CANTP_OK;
}

CanTp_StatusType CanTp_EncodeFirstFrame(const uint8_t *payload, uint16_t payload_len, uint8_t *can_data)
{
    uint8_t index;

    if ((payload == 0) || (can_data == 0))
    {
        return CANTP_ERR_PARAM;
    }
    if ((payload_len <= CANTP_SF_MAX_PAYLOAD_LEN) || (payload_len > CANTP_MAX_PDU_LEN))
    {
        return CANTP_ERR_LENGTH;
    }

    CanTp_ClearCanData(can_data);
    can_data[0] = (uint8_t)(0x10u | ((payload_len >> 8) & 0x0Fu));
    can_data[1] = (uint8_t)(payload_len & 0xFFu);
    for (index = 0u; index < CANTP_FIRST_FRAME_DATA_LEN; index++)
    {
        can_data[2u + index] = payload[index];
    }
    return CANTP_OK;
}

CanTp_StatusType CanTp_EncodeConsecutiveFrame(uint8_t sequence_number,
                                               const uint8_t *payload,
                                               uint8_t payload_len,
                                               uint8_t *can_data)
{
    uint8_t index;

    if ((payload == 0) || (can_data == 0))
    {
        return CANTP_ERR_PARAM;
    }
    if ((sequence_number > 0x0Fu) || (payload_len == 0u) || (payload_len > CANTP_CONSECUTIVE_FRAME_DATA_LEN))
    {
        return CANTP_ERR_LENGTH;
    }

    CanTp_ClearCanData(can_data);
    can_data[0] = (uint8_t)(0x20u | sequence_number);
    for (index = 0u; index < payload_len; index++)
    {
        can_data[1u + index] = payload[index];
    }
    return CANTP_OK;
}

CanTp_StatusType CanTp_EncodeFlowControlCts(uint8_t block_size, uint8_t st_min, uint8_t *can_data)
{
    if (can_data == 0)
    {
        return CANTP_ERR_PARAM;
    }

    CanTp_ClearCanData(can_data);
    can_data[0] = 0x30u;
    can_data[1] = block_size;
    can_data[2] = st_min;
    return CANTP_OK;
}

void CanTp_Init(void)
{
    s_tx_session.state = CANTP_TX_IDLE;
    s_tx_session.can_id = 0u;
    s_tx_session.deadline_ms = 0u;
    s_tx_session.next_cf_ms = 0u;
    s_tx_session.payload_len = 0u;
    s_tx_session.offset = 0u;
    s_tx_session.sequence_number = 1u;
    s_tx_session.block_size = 0u;
    s_tx_session.cf_in_block = 0u;
    s_tx_session.st_min_ms = 0u;
    s_rx_session.state = CANTP_RX_IDLE;
    s_rx_session.deadline_ms = 0u;
    s_rx_session.payload_len = 0u;
    s_rx_session.offset = 0u;
    s_rx_session.expected_sequence_number = 1u;
    s_rx_session.cf_in_block = 0u;
    s_rx_session.pdu_available = 0u;
    s_pending_error = CANTP_OK;
    s_runtime_stats.tx_multi_frame_start_count = 0u;
    s_runtime_stats.rx_multi_frame_start_count = 0u;
    s_runtime_stats.tx_multi_frame_complete_count = 0u;
    s_runtime_stats.rx_multi_frame_complete_count = 0u;
    s_runtime_stats.flow_control_tx_count = 0u;
    s_runtime_stats.flow_control_rx_count = 0u;
    s_runtime_stats.error_count = 0u;
    s_runtime_stats.state_flags = 0u;
    s_runtime_stats.last_error = (uint8_t)CANTP_OK;
}

CanTp_StatusType CanTp_Transmit(uint32_t tx_can_id,
                                const uint8_t *payload,
                                uint16_t payload_len,
                                uint32_t now_ms)
{
    uint8_t can_data[CANTP_CAN_DATA_LEN] = {0};
    uint16_t index;

    if ((payload == 0) || (payload_len == 0u) || (payload_len > CANTP_MAX_PDU_LEN))
    {
        return CANTP_ERR_PARAM;
    }
    if (s_tx_session.state != CANTP_TX_IDLE)
    {
        return CANTP_ERR_BUSY;
    }

    if (payload_len <= CANTP_SF_MAX_PAYLOAD_LEN)
    {
        if (CanTp_EncodeSingleFrame(payload, (uint8_t)payload_len, can_data) != CANTP_SF_OK)
        {
            return CANTP_ERR_LENGTH;
        }
        return (CanIf_Transmit(tx_can_id, can_data, CANTP_CAN_DATA_LEN) == CANIF_OK)
                   ? CANTP_OK
                   : CANTP_ERR_TRANSMIT;
    }

    if (CanTp_EncodeFirstFrame(payload, payload_len, can_data) != CANTP_OK)
    {
        return CANTP_ERR_LENGTH;
    }
    if (CanIf_Transmit(tx_can_id, can_data, CANTP_CAN_DATA_LEN) != CANIF_OK)
    {
        return CANTP_ERR_TRANSMIT;
    }

    for (index = 0u; index < payload_len; index++)
    {
        s_tx_session.payload[index] = payload[index];
    }
    s_tx_session.state = CANTP_TX_WAIT_FC;
    s_tx_session.can_id = tx_can_id;
    s_tx_session.deadline_ms = now_ms + CANTP_N_BS_TIMEOUT_MS;
    s_tx_session.next_cf_ms = now_ms;
    s_tx_session.payload_len = payload_len;
    s_tx_session.offset = CANTP_FIRST_FRAME_DATA_LEN;
    s_tx_session.sequence_number = 1u;
    s_tx_session.block_size = 0u;
    s_tx_session.cf_in_block = 0u;
    s_runtime_stats.tx_multi_frame_start_count++;
    return CANTP_OK;
}

CanTp_StatusType CanTp_HandleFlowControl(const uint8_t *can_data, uint8_t can_dlc, uint32_t now_ms)
{
    CanTp_PciType pci_type;
    uint8_t flow_status;

    if (CanTp_GetPciType(can_data, can_dlc, &pci_type) != CANTP_OK)
    {
        return CANTP_ERR_FRAME_TYPE;
    }
    if ((pci_type != CANTP_PCI_FLOW_CONTROL) || (can_dlc < 3u))
    {
        return CANTP_ERR_FRAME_TYPE;
    }
    if (s_tx_session.state != CANTP_TX_WAIT_FC)
    {
        return CANTP_ERR_BUSY;
    }

    flow_status = (uint8_t)(can_data[0] & 0x0Fu);
    if (flow_status != 0u)
    {
        s_tx_session.state = CANTP_TX_IDLE;
        CanTp_SetPendingError(CANTP_ERR_FRAME_TYPE);
        return CANTP_ERR_FRAME_TYPE;
    }

    if (CanTp_DecodeStMinMs(can_data[2], &s_tx_session.st_min_ms) == 0u)
    {
        s_tx_session.state = CANTP_TX_IDLE;
        CanTp_SetPendingError(CANTP_ERR_FRAME_TYPE);
        return CANTP_ERR_FRAME_TYPE;
    }
    /* V2.11/V2.12: receiver-defined block and CF pacing contract. */
    s_tx_session.block_size = can_data[1];
    s_tx_session.cf_in_block = 0u;
    s_tx_session.next_cf_ms = now_ms;
    s_tx_session.state = CANTP_TX_SEND_CF;
    s_runtime_stats.flow_control_rx_count++;
    return CANTP_OK;
}

CanTp_StatusType CanTp_RxIndication(const uint8_t *can_data,
                                    uint8_t can_dlc,
                                    uint32_t response_can_id,
                                    uint32_t now_ms)
{
    CanTp_PciType pci_type;
    uint16_t total_len;
    uint16_t remaining;
    uint8_t chunk_len;
    uint8_t sequence_number;
    uint8_t can_response[CANTP_CAN_DATA_LEN] = {0};
    uint8_t index;

    if (CanTp_GetPciType(can_data, can_dlc, &pci_type) != CANTP_OK)
    {
        CanTp_SetPendingError(CANTP_ERR_FRAME_TYPE);
        return CANTP_ERR_FRAME_TYPE;
    }

    if (pci_type == CANTP_PCI_FIRST_FRAME)
    {
        if (can_dlc < CANTP_CAN_DATA_LEN)
        {
            CanTp_SetPendingError(CANTP_ERR_LENGTH);
            return CANTP_ERR_LENGTH;
        }
        if ((s_rx_session.state != CANTP_RX_IDLE) || (s_rx_session.pdu_available != 0u))
        {
            CanTp_SetPendingError(CANTP_ERR_BUSY);
            return CANTP_ERR_BUSY;
        }

        total_len = (uint16_t)(((uint16_t)(can_data[0] & 0x0Fu) << 8) | can_data[1]);
        if ((total_len <= CANTP_SF_MAX_PAYLOAD_LEN) || (total_len > CANTP_MAX_PDU_LEN))
        {
            CanTp_SetPendingError(CANTP_ERR_LENGTH);
            return CANTP_ERR_LENGTH;
        }

        for (index = 0u; index < CANTP_FIRST_FRAME_DATA_LEN; index++)
        {
            s_rx_session.payload[index] = can_data[2u + index];
        }
        s_rx_session.state = CANTP_RX_WAIT_CF;
        s_rx_session.deadline_ms = now_ms + CANTP_N_CR_TIMEOUT_MS;
        s_rx_session.payload_len = total_len;
        s_rx_session.offset = CANTP_FIRST_FRAME_DATA_LEN;
        s_rx_session.expected_sequence_number = 1u;
        s_rx_session.cf_in_block = 0u;

        if (CanTp_EncodeFlowControlCts(CANTP_RX_FLOW_CONTROL_BLOCK_SIZE,
                                       CANTP_RX_FLOW_CONTROL_ST_MIN_MS,
                                       can_response) != CANTP_OK)
        {
            s_rx_session.state = CANTP_RX_IDLE;
            CanTp_SetPendingError(CANTP_ERR_TRANSMIT);
            return CANTP_ERR_TRANSMIT;
        }
        if (CanIf_Transmit(response_can_id, can_response, CANTP_CAN_DATA_LEN) != CANIF_OK)
        {
            s_rx_session.state = CANTP_RX_IDLE;
            CanTp_SetPendingError(CANTP_ERR_TRANSMIT);
            return CANTP_ERR_TRANSMIT;
        }
        s_runtime_stats.rx_multi_frame_start_count++;
        s_runtime_stats.flow_control_tx_count++;
        return CANTP_OK;
    }

    if (pci_type != CANTP_PCI_CONSECUTIVE_FRAME)
    {
        CanTp_SetPendingError(CANTP_ERR_FRAME_TYPE);
        return CANTP_ERR_FRAME_TYPE;
    }
    if (s_rx_session.state != CANTP_RX_WAIT_CF)
    {
        CanTp_SetPendingError(CANTP_ERR_BUSY);
        return CANTP_ERR_BUSY;
    }

    sequence_number = (uint8_t)(can_data[0] & 0x0Fu);
    if (sequence_number != s_rx_session.expected_sequence_number)
    {
        s_rx_session.state = CANTP_RX_IDLE;
        CanTp_SetPendingError(CANTP_ERR_SEQUENCE);
        return CANTP_ERR_SEQUENCE;
    }

    remaining = (uint16_t)(s_rx_session.payload_len - s_rx_session.offset);
    chunk_len = (remaining > CANTP_CONSECUTIVE_FRAME_DATA_LEN)
                    ? CANTP_CONSECUTIVE_FRAME_DATA_LEN
                    : (uint8_t)remaining;
    if (can_dlc < (uint8_t)(1u + chunk_len))
    {
        s_rx_session.state = CANTP_RX_IDLE;
        CanTp_SetPendingError(CANTP_ERR_LENGTH);
        return CANTP_ERR_LENGTH;
    }
    for (index = 0u; index < chunk_len; index++)
    {
        s_rx_session.payload[s_rx_session.offset + index] = can_data[1u + index];
    }
    s_rx_session.offset = (uint16_t)(s_rx_session.offset + chunk_len);
    s_rx_session.expected_sequence_number = (uint8_t)((s_rx_session.expected_sequence_number + 1u) & 0x0Fu);
    s_rx_session.cf_in_block++;
    s_rx_session.deadline_ms = now_ms + CANTP_N_CR_TIMEOUT_MS;

    if (s_rx_session.offset == s_rx_session.payload_len)
    {
        s_rx_session.state = CANTP_RX_IDLE;
        s_rx_session.pdu_available = 1u;
        s_runtime_stats.rx_multi_frame_complete_count++;
    }
    else if (s_rx_session.cf_in_block >= CANTP_RX_FLOW_CONTROL_BLOCK_SIZE)
    {
        if ((CanTp_EncodeFlowControlCts(CANTP_RX_FLOW_CONTROL_BLOCK_SIZE,
                                        CANTP_RX_FLOW_CONTROL_ST_MIN_MS,
                                        can_response) != CANTP_OK) ||
            (CanIf_Transmit(response_can_id, can_response, CANTP_CAN_DATA_LEN) != CANIF_OK))
        {
            s_rx_session.state = CANTP_RX_IDLE;
            CanTp_SetPendingError(CANTP_ERR_TRANSMIT);
            return CANTP_ERR_TRANSMIT;
        }
        s_rx_session.cf_in_block = 0u;
        s_runtime_stats.flow_control_tx_count++;
    }
    return CANTP_OK;
}

uint8_t CanTp_IsRxPduAvailable(void)
{
    return s_rx_session.pdu_available;
}

CanTp_StatusType CanTp_GetRxPdu(uint8_t *buffer, uint16_t buffer_len, uint16_t *pdu_len)
{
    uint16_t index;

    if ((buffer == 0) || (pdu_len == 0))
    {
        return CANTP_ERR_PARAM;
    }
    if (s_rx_session.pdu_available == 0u)
    {
        return CANTP_ERR_BUSY;
    }
    if (buffer_len < s_rx_session.payload_len)
    {
        return CANTP_ERR_LENGTH;
    }

    for (index = 0u; index < s_rx_session.payload_len; index++)
    {
        buffer[index] = s_rx_session.payload[index];
    }
    *pdu_len = s_rx_session.payload_len;
    s_rx_session.pdu_available = 0u;
    return CANTP_OK;
}

CanTp_StatusType CanTp_GetAndClearError(void)
{
    CanTp_StatusType status = s_pending_error;

    s_pending_error = CANTP_OK;
    return status;
}

CanTp_RuntimeStatsType CanTp_GetRuntimeStats(void)
{
    CanTp_RuntimeStatsType stats = s_runtime_stats;

    if (s_tx_session.state != CANTP_TX_IDLE)
    {
        stats.state_flags |= 0x01u;
    }
    if (s_rx_session.state != CANTP_RX_IDLE)
    {
        stats.state_flags |= 0x02u;
    }
    return stats;
}

void CanTp_MainFunction(uint32_t now_ms)
{
    uint8_t can_data[CANTP_CAN_DATA_LEN] = {0};
    uint8_t chunk_len;

    if ((s_rx_session.state == CANTP_RX_WAIT_CF) &&
        ((int32_t)(now_ms - s_rx_session.deadline_ms) >= 0))
    {
        s_rx_session.state = CANTP_RX_IDLE;
        s_rx_session.offset = 0u;
        s_rx_session.payload_len = 0u;
        CanTp_SetPendingError(CANTP_ERR_TIMEOUT);
    }

    if (s_tx_session.state == CANTP_TX_WAIT_FC)
    {
        if ((int32_t)(now_ms - s_tx_session.deadline_ms) >= 0)
        {
            s_tx_session.state = CANTP_TX_IDLE;
            CanTp_SetPendingError(CANTP_ERR_TIMEOUT);
        }
        return;
    }

    if ((s_tx_session.state == CANTP_TX_SEND_CF) &&
        ((int32_t)(now_ms - s_tx_session.next_cf_ms) < 0))
    {
        return;
    }

    while ((s_tx_session.state == CANTP_TX_SEND_CF) && (s_tx_session.offset < s_tx_session.payload_len))
    {
        uint16_t remaining = (uint16_t)(s_tx_session.payload_len - s_tx_session.offset);

        chunk_len = (remaining > CANTP_CONSECUTIVE_FRAME_DATA_LEN)
                        ? CANTP_CONSECUTIVE_FRAME_DATA_LEN
                        : (uint8_t)remaining;
        if (CanTp_EncodeConsecutiveFrame(s_tx_session.sequence_number,
                                         &s_tx_session.payload[s_tx_session.offset],
                                         chunk_len,
                                         can_data) != CANTP_OK)
        {
            s_tx_session.state = CANTP_TX_IDLE;
            return;
        }
        if (CanIf_Transmit(s_tx_session.can_id, can_data, CANTP_CAN_DATA_LEN) != CANIF_OK)
        {
            return;
        }

        s_tx_session.offset = (uint16_t)(s_tx_session.offset + chunk_len);
        s_tx_session.sequence_number = (uint8_t)((s_tx_session.sequence_number + 1u) & 0x0Fu);
        s_tx_session.cf_in_block++;
        s_tx_session.next_cf_ms = now_ms + s_tx_session.st_min_ms;

        if ((s_tx_session.offset < s_tx_session.payload_len) &&
            (s_tx_session.block_size != 0u) &&
            (s_tx_session.cf_in_block >= s_tx_session.block_size))
        {
            s_tx_session.state = CANTP_TX_WAIT_FC;
            s_tx_session.deadline_ms = now_ms + CANTP_N_BS_TIMEOUT_MS;
            return;
        }

        if (s_tx_session.st_min_ms != 0u)
        {
            return;
        }
    }

    if (s_tx_session.state == CANTP_TX_SEND_CF)
    {
        s_tx_session.state = CANTP_TX_IDLE;
        s_runtime_stats.tx_multi_frame_complete_count++;
    }
}
