#ifndef CANTP_SF_H
#define CANTP_SF_H

#include <stdint.h>

#define CANTP_SF_MAX_PAYLOAD_LEN 7u
#define CANTP_CAN_DATA_LEN 8u

typedef enum
{
    CANTP_SF_OK = 0u,
    CANTP_SF_ERR_NOT_SINGLE_FRAME,
    CANTP_SF_ERR_LENGTH,
    CANTP_SF_ERR_PARAM
} CanTpSf_StatusType;

CanTpSf_StatusType CanTpSf_Decode(const uint8_t *can_data, uint8_t can_dlc, const uint8_t **payload, uint8_t *payload_len);
CanTpSf_StatusType CanTpSf_Encode(const uint8_t *payload, uint8_t payload_len, uint8_t *can_data);

#endif
