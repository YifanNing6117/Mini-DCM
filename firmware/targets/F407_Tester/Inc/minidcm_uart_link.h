#ifndef MINIDCM_UART_LINK_H
#define MINIDCM_UART_LINK_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MINIDCM_UART_SOF0 0xA5u
#define MINIDCM_UART_SOF1 0x5Au
#define MINIDCM_UART_VERSION 0x01u
#define MINIDCM_UART_HEADER_SIZE 5u
#define MINIDCM_UART_CRC_SIZE 2u
#define MINIDCM_UART_MIN_BODY_SIZE 3u
#define MINIDCM_UART_MAX_PAYLOAD_SIZE 128u
#define MINIDCM_UART_MAX_BODY_SIZE (MINIDCM_UART_MIN_BODY_SIZE + MINIDCM_UART_MAX_PAYLOAD_SIZE)
#define MINIDCM_UART_MAX_FRAME_SIZE (2u + 2u + MINIDCM_UART_MAX_BODY_SIZE + 2u)

typedef enum {
    MINIDCM_UART_OK = 0,
    MINIDCM_UART_ERR_ARG = -1,
    MINIDCM_UART_ERR_SMALL_BUFFER = -2,
    MINIDCM_UART_ERR_BAD_LENGTH = -3,
    MINIDCM_UART_ERR_BAD_VERSION = -4,
    MINIDCM_UART_ERR_BAD_CRC = -5,
    MINIDCM_UART_ERR_NO_FRAME = -6
} MinidcmUartStatus;

typedef enum {
    MINIDCM_UART_CMD_PING = 0x01u,
    MINIDCM_UART_CMD_GET_INFO = 0x02u,
    MINIDCM_UART_CMD_SET_MODE = 0x03u,
    MINIDCM_UART_CMD_CAN_TX = 0x10u,
    MINIDCM_UART_CMD_CAN_RX_POLL = 0x11u,
    MINIDCM_UART_CMD_GET_RUNTIME = 0x12u,
    MINIDCM_UART_CMD_CAN_TX_BURST = 0x13u,
    MINIDCM_UART_CMD_TRACE_STATS = 0x14u,
    MINIDCM_UART_CMD_TRACE_BAD_CRC = 0x15u,
    MINIDCM_UART_CMD_UDS_REQUEST = 0x20u,
    MINIDCM_UART_CMD_ERROR = 0xFFu
} MinidcmUartCommand;

typedef struct {
    uint8_t version;
    uint8_t seq;
    uint8_t cmd;
    const uint8_t *payload;
    uint16_t payload_len;
} MinidcmUartFrameView;

typedef struct {
    uint8_t buffer[MINIDCM_UART_MAX_FRAME_SIZE];
    uint16_t length;
} MinidcmUartFrameBuffer;

uint16_t Minidcm_UartCrc16Ccitt(const uint8_t *data, uint16_t length);

MinidcmUartStatus Minidcm_UartBuildFrame(
    uint8_t seq,
    uint8_t cmd,
    const uint8_t *payload,
    uint16_t payload_len,
    uint8_t *out,
    uint16_t out_capacity,
    uint16_t *out_len);

MinidcmUartStatus Minidcm_UartParseFrame(
    const uint8_t *data,
    uint16_t data_len,
    MinidcmUartFrameView *frame,
    uint16_t *consumed);

uint8_t Minidcm_UartResponseCmd(uint8_t request_cmd);

#ifdef __cplusplus
}
#endif

#endif
