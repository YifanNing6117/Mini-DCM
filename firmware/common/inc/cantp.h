#ifndef CANTP_H
#define CANTP_H

#include <stdint.h>
#include "cantp_sf.h"

#define CANTP_MAX_PDU_LEN                 256u
#define CANTP_FIRST_FRAME_DATA_LEN         6u
#define CANTP_CONSECUTIVE_FRAME_DATA_LEN   7u

typedef enum
{
    CANTP_OK = 0u,
    CANTP_ERR_PARAM,
    CANTP_ERR_LENGTH,
    CANTP_ERR_FRAME_TYPE,
    CANTP_ERR_SEQUENCE,
    CANTP_ERR_BUSY,
    CANTP_ERR_TRANSMIT,
    CANTP_ERR_TIMEOUT
} CanTp_StatusType;

typedef enum
{
    CANTP_PCI_SINGLE_FRAME = 0x00u,
    CANTP_PCI_FIRST_FRAME = 0x01u,
    CANTP_PCI_CONSECUTIVE_FRAME = 0x02u,
    CANTP_PCI_FLOW_CONTROL = 0x03u
} CanTp_PciType;

typedef struct
{
    const uint8_t *payload;
    uint8_t payload_len;
} CanTp_FrameViewType;

typedef struct
{
    uint32_t tx_multi_frame_start_count;
    uint32_t rx_multi_frame_start_count;
    uint32_t tx_multi_frame_complete_count;
    uint32_t rx_multi_frame_complete_count;
    uint32_t flow_control_tx_count;
    uint32_t flow_control_rx_count;
    uint32_t error_count;
    uint8_t state_flags;
    uint8_t last_error;
} CanTp_RuntimeStatsType;

CanTpSf_StatusType CanTp_DecodeSingleFrame(const uint8_t *can_data, uint8_t can_dlc, CanTp_FrameViewType *frame);
CanTpSf_StatusType CanTp_EncodeSingleFrame(const uint8_t *payload, uint8_t payload_len, uint8_t *can_data);

/* V2.9-1 wire-level codec. Session state is added in a later V2.9 gate. */
CanTp_StatusType CanTp_GetPciType(const uint8_t *can_data, uint8_t can_dlc, CanTp_PciType *pci_type);
CanTp_StatusType CanTp_EncodeFirstFrame(const uint8_t *payload, uint16_t payload_len, uint8_t *can_data);
CanTp_StatusType CanTp_EncodeConsecutiveFrame(uint8_t sequence_number,
                                               const uint8_t *payload,
                                               uint8_t payload_len,
                                               uint8_t *can_data);
CanTp_StatusType CanTp_EncodeFlowControlCts(uint8_t block_size, uint8_t st_min, uint8_t *can_data);

/* V2.9-2 ECU TX session. Requests remain Single Frame at this gate. */
void CanTp_Init(void);
CanTp_StatusType CanTp_Transmit(uint32_t tx_can_id,
                                const uint8_t *payload,
                                uint16_t payload_len,
                                uint32_t now_ms);
CanTp_StatusType CanTp_HandleFlowControl(const uint8_t *can_data, uint8_t can_dlc, uint32_t now_ms);
CanTp_StatusType CanTp_RxIndication(const uint8_t *can_data,
                                    uint8_t can_dlc,
                                    uint32_t response_can_id,
                                    uint32_t now_ms);
uint8_t CanTp_IsRxPduAvailable(void);
CanTp_StatusType CanTp_GetRxPdu(uint8_t *buffer,
                                uint16_t buffer_len,
                                uint16_t *pdu_len);
CanTp_StatusType CanTp_GetAndClearError(void);
CanTp_RuntimeStatsType CanTp_GetRuntimeStats(void);
void CanTp_MainFunction(uint32_t now_ms);

#endif
