#include "minidcm_uart_link.h"

static uint16_t read_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static void write_u16_le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value & 0xFFu);
    data[1] = (uint8_t)((value >> 8) & 0xFFu);
}

uint16_t Minidcm_UartCrc16Ccitt(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFu;
    uint16_t i;

    if (data == 0 && length != 0u) {
        return 0u;
    }

    for (i = 0; i < length; i++) {
        uint8_t bit;
        crc ^= (uint16_t)data[i] << 8;
        for (bit = 0; bit < 8u; bit++) {
            if ((crc & 0x8000u) != 0u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }

    return crc;
}

MinidcmUartStatus Minidcm_UartBuildFrame(
    uint8_t seq,
    uint8_t cmd,
    const uint8_t *payload,
    uint16_t payload_len,
    uint8_t *out,
    uint16_t out_capacity,
    uint16_t *out_len)
{
    uint16_t body_len;
    uint16_t frame_len;
    uint16_t crc;
    uint16_t i;

    if (out == 0 || out_len == 0 || (payload == 0 && payload_len != 0u)) {
        return MINIDCM_UART_ERR_ARG;
    }
    if (payload_len > MINIDCM_UART_MAX_PAYLOAD_SIZE) {
        return MINIDCM_UART_ERR_BAD_LENGTH;
    }

    body_len = (uint16_t)(MINIDCM_UART_MIN_BODY_SIZE + payload_len);
    frame_len = (uint16_t)(2u + 2u + body_len + MINIDCM_UART_CRC_SIZE);
    if (out_capacity < frame_len) {
        return MINIDCM_UART_ERR_SMALL_BUFFER;
    }

    out[0] = MINIDCM_UART_SOF0;
    out[1] = MINIDCM_UART_SOF1;
    write_u16_le(&out[2], body_len);
    out[4] = MINIDCM_UART_VERSION;
    out[5] = seq;
    out[6] = cmd;
    for (i = 0; i < payload_len; i++) {
        out[7u + i] = payload[i];
    }

    crc = Minidcm_UartCrc16Ccitt(&out[2], (uint16_t)(2u + body_len));
    write_u16_le(&out[7u + payload_len], crc);
    *out_len = frame_len;

    return MINIDCM_UART_OK;
}

MinidcmUartStatus Minidcm_UartParseFrame(
    const uint8_t *data,
    uint16_t data_len,
    MinidcmUartFrameView *frame,
    uint16_t *consumed)
{
    uint16_t offset;
    uint16_t body_len;
    uint16_t frame_len;
    uint16_t expected_crc;
    uint16_t actual_crc;

    if (data == 0 || frame == 0 || consumed == 0) {
        return MINIDCM_UART_ERR_ARG;
    }

    *consumed = 0u;
    if (data_len < 2u) {
        return MINIDCM_UART_ERR_NO_FRAME;
    }

    for (offset = 0; (uint16_t)(offset + 1u) < data_len; offset++) {
        if (data[offset] == MINIDCM_UART_SOF0 && data[offset + 1u] == MINIDCM_UART_SOF1) {
            break;
        }
    }
    if ((uint16_t)(offset + 1u) >= data_len) {
        *consumed = (uint16_t)(data_len - 1u);
        return MINIDCM_UART_ERR_NO_FRAME;
    }
    if (offset > 0u) {
        *consumed = offset;
        return MINIDCM_UART_ERR_NO_FRAME;
    }
    if (data_len < MINIDCM_UART_HEADER_SIZE) {
        return MINIDCM_UART_ERR_NO_FRAME;
    }

    body_len = read_u16_le(&data[2]);
    if (body_len < MINIDCM_UART_MIN_BODY_SIZE || body_len > MINIDCM_UART_MAX_BODY_SIZE) {
        *consumed = 2u;
        return MINIDCM_UART_ERR_BAD_LENGTH;
    }

    frame_len = (uint16_t)(2u + 2u + body_len + MINIDCM_UART_CRC_SIZE);
    if (data_len < frame_len) {
        return MINIDCM_UART_ERR_NO_FRAME;
    }
    if (data[4] != MINIDCM_UART_VERSION) {
        *consumed = frame_len;
        return MINIDCM_UART_ERR_BAD_VERSION;
    }

    expected_crc = read_u16_le(&data[frame_len - 2u]);
    actual_crc = Minidcm_UartCrc16Ccitt(&data[2], (uint16_t)(2u + body_len));
    if (expected_crc != actual_crc) {
        *consumed = 2u;
        return MINIDCM_UART_ERR_BAD_CRC;
    }

    frame->version = data[4];
    frame->seq = data[5];
    frame->cmd = data[6];
    frame->payload = &data[7];
    frame->payload_len = (uint16_t)(body_len - MINIDCM_UART_MIN_BODY_SIZE);
    *consumed = frame_len;
    return MINIDCM_UART_OK;
}

uint8_t Minidcm_UartResponseCmd(uint8_t request_cmd)
{
    if (request_cmd < 0x80u) {
        return (uint8_t)(request_cmd | 0x80u);
    }

    return MINIDCM_UART_CMD_ERROR;
}
