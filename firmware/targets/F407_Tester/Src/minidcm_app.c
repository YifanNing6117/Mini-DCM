#include "minidcm_app.h"

#include <string.h>
#include "minidcm_uart_link.h"
#include "trace_link.h"

#define MINIDCM_RX_RING_SIZE 256u
#define MINIDCM_CAN_ECU_RESPONSE_ID 0x602u
#define MINIDCM_CAN_TX_QUEUE_CAPACITY 8u
#define MINIDCM_CAN_RX_QUEUE_CAPACITY 8u

typedef struct {
    uint16_t std_id;
    uint8_t dlc;
    uint8_t data[8];
} MinidcmCanTxQueueItem;

typedef struct {
    uint16_t std_id;
    uint8_t dlc;
    uint8_t data[8];
} MinidcmCanRxQueueItem;

#ifndef MINIDCM_INFO_TEXT
#define MINIDCM_INFO_TEXT "Mini-DCM Tester F407 USART1"
#endif

static UART_HandleTypeDef *s_uart;
static CAN_HandleTypeDef *s_can;
static volatile uint8_t s_rx_ring[MINIDCM_RX_RING_SIZE];
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;
static uint8_t s_parse_buffer[MINIDCM_UART_MAX_FRAME_SIZE * 2u];
static uint16_t s_parse_len;

static volatile uint8_t s_can_error_seen;
static volatile uint32_t s_can_rx_count;
static volatile uint32_t s_can_error_count;
static volatile MinidcmCanRxQueueItem s_can_rx_queue[MINIDCM_CAN_RX_QUEUE_CAPACITY];
static volatile uint8_t s_can_rx_head;
static volatile uint8_t s_can_rx_tail;
static volatile uint8_t s_can_rx_queue_count;
static volatile uint8_t s_can_rx_queue_high_water;
static volatile uint16_t s_can_rx_queue_reject_count;
static volatile uint32_t s_can_rx_queue_enqueue_count;
static volatile uint32_t s_runtime_uart_isr_notify_count;
static volatile uint32_t s_runtime_can_isr_notify_count;
static volatile uint32_t s_runtime_bridge_wakeup_count;
static volatile uint32_t s_runtime_heartbeat_count;
static volatile uint16_t s_runtime_bridge_stack_hwm;
static volatile uint16_t s_runtime_heartbeat_stack_hwm;
static MinidcmCanTxQueueItem s_can_tx_queue[MINIDCM_CAN_TX_QUEUE_CAPACITY];
static uint8_t s_can_tx_head;
static uint8_t s_can_tx_tail;
static uint8_t s_can_tx_count;
static uint8_t s_can_tx_high_water;
static uint16_t s_can_tx_reject_count;
static uint32_t s_can_tx_enqueue_count;
static uint32_t s_trace_last_can_error_count;
static uint16_t s_trace_last_tx_reject_count;
static uint16_t s_trace_last_rx_reject_count;

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

static void can_start_or_error(void)
{
    CAN_FilterTypeDef filter;

    if (s_can == 0) {
        return;
    }

    memset(&filter, 0, sizeof(filter));
    filter.FilterBank = 0u;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh = (uint16_t)(MINIDCM_CAN_ECU_RESPONSE_ID << 5);
    filter.FilterIdLow = 0u;
    filter.FilterMaskIdHigh = (uint16_t)(0x7FFu << 5);
    filter.FilterMaskIdLow = 0u;
    filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter.FilterActivation = ENABLE;
    filter.SlaveStartFilterBank = 14u;

    if (HAL_CAN_ConfigFilter(s_can, &filter) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_CAN_Start(s_can) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_CAN_ActivateNotification(s_can, CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_ERROR | CAN_IT_BUSOFF) != HAL_OK) {
        Error_Handler();
    }
}

static uint8_t can_tx_enqueue(uint16_t std_id, uint8_t dlc, const uint8_t *data)
{
    MinidcmCanTxQueueItem *item;

    if (s_can_tx_count >= MINIDCM_CAN_TX_QUEUE_CAPACITY) {
        s_can_tx_reject_count++;
        return 0u;
    }

    item = &s_can_tx_queue[s_can_tx_head];
    item->std_id = std_id;
    item->dlc = dlc;
    memset(item->data, 0, sizeof(item->data));
    if (dlc > 0u) {
        memcpy(item->data, data, dlc);
    }

    s_can_tx_head = (uint8_t)((s_can_tx_head + 1u) % MINIDCM_CAN_TX_QUEUE_CAPACITY);
    s_can_tx_count++;
    s_can_tx_enqueue_count++;
    if (s_can_tx_count > s_can_tx_high_water) {
        s_can_tx_high_water = s_can_tx_count;
    }
    return 1u;
}

static void can_tx_process_queue(void)
{
    CAN_TxHeaderTypeDef tx_header;
    uint32_t mailbox;

    if (s_can == 0) {
        return;
    }

    while (s_can_tx_count > 0u) {
        const MinidcmCanTxQueueItem *item = &s_can_tx_queue[s_can_tx_tail];

        memset(&tx_header, 0, sizeof(tx_header));
        tx_header.StdId = item->std_id;
        tx_header.IDE = CAN_ID_STD;
        tx_header.RTR = CAN_RTR_DATA;
        tx_header.DLC = item->dlc;
        tx_header.TransmitGlobalTime = DISABLE;

        if (HAL_CAN_AddTxMessage(s_can, &tx_header, (uint8_t *)item->data, &mailbox) != HAL_OK) {
            break;
        }

        (void)TraceLink_EmitCan(TRACE_SOURCE_TESTER, TRACE_EVENT_CAN_TX,
                                TRACE_SEVERITY_INFO, item->std_id,
                                item->dlc, item->data, HAL_GetTick());
        s_can_tx_tail = (uint8_t)((s_can_tx_tail + 1u) % MINIDCM_CAN_TX_QUEUE_CAPACITY);
        s_can_tx_count--;
    }
}

static void handle_ping(const MinidcmUartFrameView *frame)
{
    send_frame(
        frame->seq,
        Minidcm_UartResponseCmd(frame->cmd),
        frame->payload,
        frame->payload_len);
}

static void handle_get_info(const MinidcmUartFrameView *frame)
{
    const uint8_t *info = (const uint8_t *)MINIDCM_INFO_TEXT;
    send_frame(
        frame->seq,
        Minidcm_UartResponseCmd(frame->cmd),
        info,
        (uint16_t)strlen(MINIDCM_INFO_TEXT));
}

static void handle_can_tx(const MinidcmUartFrameView *frame)
{
    uint8_t tx_data[8] = {0};
    uint8_t response[12];
    uint16_t std_id;
    uint8_t dlc;
    uint8_t free_level;
    uint8_t state;
    uint32_t error_code;

    if (s_can == 0) {
        send_error(frame->seq, frame->cmd, 0x10u);
        return;
    }
    if (frame->payload_len < 3u) {
        send_error(frame->seq, frame->cmd, 0x02u);
        return;
    }

    std_id = (uint16_t)frame->payload[0] | ((uint16_t)frame->payload[1] << 8);
    dlc = frame->payload[2];
    if (std_id > 0x7FFu || dlc > 8u || frame->payload_len < (uint16_t)(3u + dlc)) {
        send_error(frame->seq, frame->cmd, 0x03u);
        return;
    }
    if (dlc > 0u) {
        memcpy(tx_data, &frame->payload[3], dlc);
    }

    response[1] = (uint8_t)(std_id & 0xFFu);
    response[2] = (uint8_t)(std_id >> 8);
    response[3] = dlc;
    response[4] = 0xFFu;

    if (can_tx_enqueue(std_id, dlc, tx_data) != 0u) {
        response[0] = 0x00u;
        response[4] = 0xFEu; /* Accepted by the Tester software TX queue. */
    } else {
        response[0] = 0x01u;
    }

    free_level = (uint8_t)HAL_CAN_GetTxMailboxesFreeLevel(s_can);
    state = (uint8_t)HAL_CAN_GetState(s_can);
    error_code = HAL_CAN_GetError(s_can);
    response[5] = free_level;
    response[6] = state;
    response[7] = (uint8_t)(error_code & 0xFFu);
    response[8] = (uint8_t)((error_code >> 8) & 0xFFu);
    response[9] = (uint8_t)((error_code >> 16) & 0xFFu);
    response[10] = (uint8_t)((error_code >> 24) & 0xFFu);
    response[11] = s_can_error_seen;

    send_frame(frame->seq, Minidcm_UartResponseCmd(frame->cmd), response, sizeof(response));
}

static void handle_can_tx_burst(const MinidcmUartFrameView *frame)
{
    uint8_t tx_data[8] = {0};
    uint8_t response[9];
    uint16_t std_id;
    uint8_t dlc;
    uint8_t requested_count;
    uint8_t accepted_count = 0u;
    uint8_t rejected_count = 0u;
    uint8_t index;

    if (s_can == 0 || frame->payload_len < 4u) {
        send_error(frame->seq, frame->cmd, 0x02u);
        return;
    }

    std_id = (uint16_t)frame->payload[0] | ((uint16_t)frame->payload[1] << 8);
    dlc = frame->payload[2];
    requested_count = frame->payload[3];
    if (std_id > 0x7FFu || dlc > 8u || requested_count == 0u || frame->payload_len < (uint16_t)(4u + dlc)) {
        send_error(frame->seq, frame->cmd, 0x03u);
        return;
    }
    if (dlc > 0u) {
        memcpy(tx_data, &frame->payload[4], dlc);
    }

    for (index = 0u; index < requested_count; index++) {
        if (can_tx_enqueue(std_id, dlc, tx_data) != 0u) {
            accepted_count++;
        } else {
            rejected_count++;
        }
    }

    response[0] = (rejected_count == 0u) ? 0u : 1u;
    response[1] = (uint8_t)(std_id & 0xFFu);
    response[2] = (uint8_t)(std_id >> 8);
    response[3] = dlc;
    response[4] = requested_count;
    response[5] = accepted_count;
    response[6] = rejected_count;
    response[7] = s_can_tx_count;
    response[8] = s_can_tx_high_water;
    send_frame(frame->seq, Minidcm_UartResponseCmd(frame->cmd), response, sizeof(response));
}

static void handle_can_rx_poll(const MinidcmUartFrameView *frame)
{
    uint8_t response[16] = {0};
    uint8_t ready = 0u;
    uint32_t count;
    uint16_t id = 0u;
    uint8_t dlc = 0u;
    uint8_t data[8] = {0};

    __disable_irq();
    count = s_can_rx_count;
    if (s_can_rx_queue_count > 0u) {
        const volatile MinidcmCanRxQueueItem *item = &s_can_rx_queue[s_can_rx_tail];
        id = item->std_id;
        dlc = item->dlc;
        memcpy(data, (const void *)item->data, sizeof(data));
        s_can_rx_tail = (uint8_t)((s_can_rx_tail + 1u) % MINIDCM_CAN_RX_QUEUE_CAPACITY);
        s_can_rx_queue_count--;
        ready = 1u;
    }
    __enable_irq();

    if (ready != 0u) {
        (void)TraceLink_EmitCan(TRACE_SOURCE_ECU, TRACE_EVENT_CAN_RX,
                                TRACE_SEVERITY_INFO, id, dlc, data,
                                HAL_GetTick());
    }

    response[0] = ready;
    response[1] = (uint8_t)(count & 0xFFu);
    response[2] = (uint8_t)((count >> 8) & 0xFFu);
    response[3] = (uint8_t)((count >> 16) & 0xFFu);
    response[4] = (uint8_t)((count >> 24) & 0xFFu);

    if (ready != 0u) {
        response[5] = (uint8_t)(id & 0xFFu);
        response[6] = (uint8_t)(id >> 8);
        response[7] = dlc;
        memcpy(&response[8], data, sizeof(data));
        send_frame(frame->seq, Minidcm_UartResponseCmd(frame->cmd), response, sizeof(response));
    } else {
        send_frame(frame->seq, Minidcm_UartResponseCmd(frame->cmd), response, 5u);
    }
}

static void handle_get_runtime(const MinidcmUartFrameView *frame)
{
    uint8_t response[37];
    uint32_t uart_count;
    uint32_t can_count;
    uint32_t bridge_count;
    uint32_t heartbeat_count;
    uint16_t bridge_stack_hwm;
    uint16_t heartbeat_stack_hwm;
    uint16_t tx_queue_reject_count;
    uint32_t tx_queue_enqueue_count;
    uint8_t rx_queue_pending;
    uint8_t rx_queue_high_water;
    uint16_t rx_queue_reject_count;
    uint32_t rx_queue_enqueue_count;

    __disable_irq();
    uart_count = s_runtime_uart_isr_notify_count;
    can_count = s_runtime_can_isr_notify_count;
    bridge_count = s_runtime_bridge_wakeup_count;
    heartbeat_count = s_runtime_heartbeat_count;
    bridge_stack_hwm = s_runtime_bridge_stack_hwm;
    heartbeat_stack_hwm = s_runtime_heartbeat_stack_hwm;
    rx_queue_pending = s_can_rx_queue_count;
    rx_queue_high_water = s_can_rx_queue_high_water;
    rx_queue_reject_count = s_can_rx_queue_reject_count;
    rx_queue_enqueue_count = s_can_rx_queue_enqueue_count;
    __enable_irq();

    tx_queue_reject_count = s_can_tx_reject_count;
    tx_queue_enqueue_count = s_can_tx_enqueue_count;
    response[0] = 4u;
    memcpy(&response[1], &uart_count, sizeof(uart_count));
    memcpy(&response[5], &can_count, sizeof(can_count));
    memcpy(&response[9], &bridge_count, sizeof(bridge_count));
    memcpy(&response[13], &heartbeat_count, sizeof(heartbeat_count));
    memcpy(&response[17], &bridge_stack_hwm, sizeof(bridge_stack_hwm));
    memcpy(&response[19], &heartbeat_stack_hwm, sizeof(heartbeat_stack_hwm));
    response[21] = s_can_tx_count;
    response[22] = s_can_tx_high_water;
    memcpy(&response[23], &tx_queue_reject_count, sizeof(tx_queue_reject_count));
    memcpy(&response[25], &tx_queue_enqueue_count, sizeof(tx_queue_enqueue_count));
    response[29] = rx_queue_pending;
    response[30] = rx_queue_high_water;
    memcpy(&response[31], &rx_queue_reject_count, sizeof(rx_queue_reject_count));
    memcpy(&response[33], &rx_queue_enqueue_count, sizeof(rx_queue_enqueue_count));
    send_frame(frame->seq, Minidcm_UartResponseCmd(frame->cmd), response, sizeof(response));
}

static void handle_trace_stats(const MinidcmUartFrameView *frame)
{
    uint8_t response[41];
    TraceLinkStats stats;

    TraceLink_GetStats(&stats);
    response[0] = 1u;
    response[1] = (uint8_t)stats.state;
    response[2] = stats.ready_input;
    response[3] = stats.queue_pending;
    response[4] = stats.queue_high_water;
    memcpy(&response[5], &stats.frames_enqueued, sizeof(uint32_t));
    memcpy(&response[9], &stats.frames_sent, sizeof(uint32_t));
    memcpy(&response[13], &stats.queue_rejected, sizeof(uint32_t));
    memcpy(&response[17], &stats.dma_irq_count, sizeof(uint32_t));
    memcpy(&response[21], &stats.dma_error_count, sizeof(uint32_t));
    memcpy(&response[25], &stats.timeout_count, sizeof(uint32_t));
    memcpy(&response[29], &stats.recovery_count, sizeof(uint32_t));
    memcpy(&response[33], &stats.backpressure_count, sizeof(uint32_t));
    memcpy(&response[37], &stats.bad_crc_injected, sizeof(uint32_t));
    send_frame(frame->seq, Minidcm_UartResponseCmd(frame->cmd),
               response, sizeof(response));
}

static void handle_trace_bad_crc(const MinidcmUartFrameView *frame)
{
    uint8_t response = 0u;

    TraceLink_InjectBadCrcOnce();
    send_frame(frame->seq, Minidcm_UartResponseCmd(frame->cmd), &response, 1u);
}

static void dispatch_frame(const MinidcmUartFrameView *frame)
{
    if (frame->cmd == MINIDCM_UART_CMD_PING) {
        handle_ping(frame);
        return;
    }

    if (frame->cmd == MINIDCM_UART_CMD_GET_INFO) {
        handle_get_info(frame);
        return;
    }

    if (frame->cmd == MINIDCM_UART_CMD_GET_RUNTIME) {
        handle_get_runtime(frame);
        return;
    }

    if (frame->cmd == MINIDCM_UART_CMD_CAN_TX) {
        handle_can_tx(frame);
        return;
    }

    if (frame->cmd == MINIDCM_UART_CMD_CAN_TX_BURST) {
        handle_can_tx_burst(frame);
        return;
    }

    if (frame->cmd == MINIDCM_UART_CMD_CAN_RX_POLL) {
        handle_can_rx_poll(frame);
        return;
    }

    if (frame->cmd == MINIDCM_UART_CMD_TRACE_STATS) {
        handle_trace_stats(frame);
        return;
    }

    if (frame->cmd == MINIDCM_UART_CMD_TRACE_BAD_CRC) {
        handle_trace_bad_crc(frame);
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

static void trace_publish_fault_deltas(void)
{
    uint8_t payload[4];

    if (s_can_error_count != s_trace_last_can_error_count) {
        memcpy(payload, (const void *)&s_can_error_count, sizeof(payload));
        (void)TraceLink_Emit(TRACE_SOURCE_TESTER, TRACE_EVENT_CAN_ERROR,
                             TRACE_SEVERITY_ERROR, payload, sizeof(payload),
                             HAL_GetTick());
        s_trace_last_can_error_count = s_can_error_count;
    }
    if (s_can_tx_reject_count != s_trace_last_tx_reject_count) {
        payload[0] = (uint8_t)s_can_tx_reject_count;
        payload[1] = (uint8_t)(s_can_tx_reject_count >> 8u);
        (void)TraceLink_Emit(TRACE_SOURCE_TESTER,
                             TRACE_EVENT_TX_QUEUE_OVERFLOW,
                             TRACE_SEVERITY_WARN, payload, 2u, HAL_GetTick());
        s_trace_last_tx_reject_count = s_can_tx_reject_count;
    }
    if (s_can_rx_queue_reject_count != s_trace_last_rx_reject_count) {
        payload[0] = (uint8_t)s_can_rx_queue_reject_count;
        payload[1] = (uint8_t)(s_can_rx_queue_reject_count >> 8u);
        (void)TraceLink_Emit(TRACE_SOURCE_TESTER,
                             TRACE_EVENT_RX_QUEUE_OVERFLOW,
                             TRACE_SEVERITY_WARN, payload, 2u, HAL_GetTick());
        s_trace_last_rx_reject_count = s_can_rx_queue_reject_count;
    }
}

void Minidcm_AppInit(UART_HandleTypeDef *uart, CAN_HandleTypeDef *can)
{
    s_uart = uart;
    s_can = can;
    s_rx_head = 0u;
    s_rx_tail = 0u;
    s_parse_len = 0u;
    s_can_error_seen = 0u;
    s_can_rx_count = 0u;
    s_can_error_count = 0u;
    s_can_rx_head = 0u;
    s_can_rx_tail = 0u;
    s_can_rx_queue_count = 0u;
    s_can_rx_queue_high_water = 0u;
    s_can_rx_queue_reject_count = 0u;
    s_can_rx_queue_enqueue_count = 0u;
    memset((void *)s_can_rx_queue, 0, sizeof(s_can_rx_queue));
    s_can_tx_head = 0u;
    s_can_tx_tail = 0u;
    s_can_tx_count = 0u;
    s_can_tx_high_water = 0u;
    s_can_tx_reject_count = 0u;
    s_can_tx_enqueue_count = 0u;
    s_trace_last_can_error_count = 0u;
    s_trace_last_tx_reject_count = 0u;
    s_trace_last_rx_reject_count = 0u;
    (void)TraceLink_Init();
    can_start_or_error();
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

    can_tx_process_queue();
    trace_publish_fault_deltas();
    TraceLink_MainFunction(HAL_GetTick());
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

void Minidcm_AppOnCanRx(const CAN_RxHeaderTypeDef *header, const uint8_t *data)
{
    uint8_t dlc;
    volatile MinidcmCanRxQueueItem *item;

    if (header == 0 || data == 0) {
        return;
    }
    if (header->IDE != CAN_ID_STD || header->RTR != CAN_RTR_DATA) {
        return;
    }

    dlc = (header->DLC > 8u) ? 8u : (uint8_t)header->DLC;
    s_can_rx_count++;
    if (s_can_rx_queue_count >= MINIDCM_CAN_RX_QUEUE_CAPACITY) {
        s_can_rx_queue_reject_count++;
        return;
    }

    item = &s_can_rx_queue[s_can_rx_head];
    item->std_id = (uint16_t)(header->StdId & 0x7FFu);
    item->dlc = dlc;
    memset((void *)item->data, 0, sizeof(item->data));
    if (dlc > 0u) {
        memcpy((void *)item->data, data, dlc);
    }
    s_can_rx_head = (uint8_t)((s_can_rx_head + 1u) % MINIDCM_CAN_RX_QUEUE_CAPACITY);
    s_can_rx_queue_count++;
    s_can_rx_queue_enqueue_count++;
    if (s_can_rx_queue_count > s_can_rx_queue_high_water) {
        s_can_rx_queue_high_water = s_can_rx_queue_count;
    }
}

void Minidcm_AppOnCanError(void)
{
    s_can_error_seen = 1u;
    s_can_error_count++;
}

void Minidcm_AppOnUartIsrNotify(void) { s_runtime_uart_isr_notify_count++; }
void Minidcm_AppOnCanIsrNotify(void) { s_runtime_can_isr_notify_count++; }
void Minidcm_AppOnBridgeTaskWakeup(void) { s_runtime_bridge_wakeup_count++; }
void Minidcm_AppOnHeartbeatTask(void) { s_runtime_heartbeat_count++; }
void Minidcm_AppOnStackHighWaterMark(uint16_t bridge_words, uint16_t heartbeat_words) { s_runtime_bridge_stack_hwm = bridge_words; s_runtime_heartbeat_stack_hwm = heartbeat_words; }

