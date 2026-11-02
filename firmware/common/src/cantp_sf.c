#include "cantp_sf.h"

CanTpSf_StatusType CanTpSf_Decode(const uint8_t *can_data, uint8_t can_dlc, const uint8_t **payload, uint8_t *payload_len)
{
    uint8_t frame_type;
    uint8_t length;

    if ((can_data == 0) || (payload == 0) || (payload_len == 0))
    {
        return CANTP_SF_ERR_PARAM;
    }
    if (can_dlc < 2u)
    {
        return CANTP_SF_ERR_LENGTH;
    }

    frame_type = (uint8_t)((can_data[0] >> 4) & 0x0Fu);
    length = (uint8_t)(can_data[0] & 0x0Fu);
    if (frame_type != 0u)
    {
        return CANTP_SF_ERR_NOT_SINGLE_FRAME;
    }
    if ((length == 0u) || (length > CANTP_SF_MAX_PAYLOAD_LEN) || (can_dlc < (uint8_t)(length + 1u)))
    {
        return CANTP_SF_ERR_LENGTH;
    }

    *payload = &can_data[1];
    *payload_len = length;
    return CANTP_SF_OK;
}

CanTpSf_StatusType CanTpSf_Encode(const uint8_t *payload, uint8_t payload_len, uint8_t *can_data)
{
    uint8_t i;

    if ((payload == 0) || (can_data == 0))
    {
        return CANTP_SF_ERR_PARAM;
    }
    if ((payload_len == 0u) || (payload_len > CANTP_SF_MAX_PAYLOAD_LEN))
    {
        return CANTP_SF_ERR_LENGTH;
    }

    for (i = 0u; i < CANTP_CAN_DATA_LEN; i++)
    {
        can_data[i] = 0u;
    }

    can_data[0] = (uint8_t)(payload_len & 0x0Fu);
    for (i = 0u; i < payload_len; i++)
    {
        can_data[1u + i] = payload[i];
    }

    return CANTP_SF_OK;
}
