#include "minidcm_app.h"

#include <string.h>
#include "minidcm_uart_link.h"

#define MINIDCM_RX_RING_SIZE 256u

#ifndef MINIDCM_INFO_TEXT
#define MINIDCM_INFO_TEXT "Mini-DCM Tester F407 USART1"
#endif

static UART_HandleTypeDef *s_uart;
static volatile uint8_t s_rx_ring[MINIDCM_RX_RING_SIZE];
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;
static uint8_t s_parse_buffer[MINIDCM_UART_MAX_FRAME_SIZE * 2u];
static uint16_t s_parse_len;

static uint16_t ring_next(uint16_t value)
{
    return (uint16_t)((value + 1u) % MINIDCM_RX_RING_SIZE);
}

static int ring_pop(uint8_t *byte)
{
    if (s_rx_tail == s_rx_head) {
        return 0;
    }

    *byte = s_rx_ring[s_rx_tail];
    s_rx_tail = ring_next(s_rx_tail);
    return 1;
}

static void parse_buffer_drop(uint16_t count)
{
    if (count == 0u) {
        return;
    }
    if (count >= s_parse_len) {
        s_parse_len = 0u;
        return;
    }

    memmove(s_parse_buffer, &s_parse_buffer[count], (size_t)(s_parse_len - count));
    s_parse_len = (uint16_t)(s_parse_len - count);
}

static void send_frame(uint8_t seq, uint8_t cmd, const uint8_t *payload, uint16_t payload_len)
{
    uint8_t tx[MINIDCM_UART_MAX_FRAME_SIZE];
    uint16_t tx_len = 0u;

    if (s_uart == 0) {
        return;
    }

    if (Minidcm_UartBuildFrame(seq, cmd, payload, payload_len, tx, sizeof(tx), &tx_len) == MINIDCM_UART_OK) {
        (void)HAL_UART_Transmit(s_uart, tx, tx_len, 100u);
    }
}

static void send_error(uint8_t seq, uint8_t request_cmd, uint8_t error_code)
{
    uint8_t payload[2];
    payload[0] = request_cmd;
    payload[1] = error_code;
    send_frame(seq, MINIDCM_UART_CMD_ERROR, payload, sizeof(payload));
}

static void dispatch_frame(const MinidcmUartFrameView *frame)
{
    if (frame->cmd == MINIDCM_UART_CMD_PING) {
        send_frame(
            frame->seq,
            Minidcm_UartResponseCmd(frame->cmd),
            frame->payload,
            frame->payload_len);
        return;
    }

    if (frame->cmd == MINIDCM_UART_CMD_GET_INFO) {
        const uint8_t *info = (const uint8_t *)MINIDCM_INFO_TEXT;
        send_frame(
            frame->seq,
            Minidcm_UartResponseCmd(frame->cmd),
            info,
            (uint16_t)strlen(MINIDCM_INFO_TEXT));
        return;
    }

    send_error(frame->seq, frame->cmd, 0x01u);
}

static void parse_available_frames(void)
{
    while (s_parse_len > 0u) {
        MinidcmUartFrameView frame;
        uint16_t consumed = 0u;
        MinidcmUartStatus status = Minidcm_UartParseFrame(
            s_parse_buffer,
            s_parse_len,
            &frame,
            &consumed);

        if (consumed > 0u) {
            parse_buffer_drop(consumed);
        }

        if (status == MINIDCM_UART_OK) {
            dispatch_frame(&frame);
            continue;
        }

        if (status == MINIDCM_UART_ERR_NO_FRAME) {
            break;
        }
    }
}

void Minidcm_AppInit(UART_HandleTypeDef *uart)
{
    s_uart = uart;
    s_rx_head = 0u;
    s_rx_tail = 0u;
    s_parse_len = 0u;
}

void Minidcm_AppPoll(void)
{
    uint8_t byte;

    while (ring_pop(&byte)) {
        if (s_parse_len < sizeof(s_parse_buffer)) {
            s_parse_buffer[s_parse_len] = byte;
            s_parse_len++;
        } else {
            s_parse_len = 0u;
        }

        parse_available_frames();
    }
}

void Minidcm_AppOnRxByte(uint8_t byte)
{
    uint16_t next = ring_next(s_rx_head);
    if (next == s_rx_tail) {
        return;
    }

    s_rx_ring[s_rx_head] = byte;
    s_rx_head = next;
}
