#include "can_if.h"

#define CANIF_RX_QUEUE_SIZE 16u
#define CANIF_USE_EXACT_RX_FILTER 0u

static CAN_HandleTypeDef *s_canif_hcan;
static volatile uint32_t s_canif_tx_count;
static volatile uint32_t s_canif_rx_count;
static volatile uint32_t s_canif_rx_overflow_count;
static volatile uint32_t s_canif_error_count;
static volatile uint8_t s_canif_rx_head;
static volatile uint8_t s_canif_rx_tail;
static CanIf_FrameType s_canif_rx_queue[CANIF_RX_QUEUE_SIZE];
static CanIf_FrameType s_canif_tx_queue[CANIF_TX_QUEUE_SIZE];
static uint8_t s_canif_tx_head;
static uint8_t s_canif_tx_tail;
static uint8_t s_canif_tx_queue_count;
static uint8_t s_canif_tx_queue_high_water;
static uint16_t s_canif_tx_queue_reject_count;
static uint32_t s_canif_tx_queue_enqueue_count;
static CanIf_RxQueueBackendType s_canif_rx_queue_backend;
static uint8_t s_canif_rx_queue_backend_active;

static uint8_t CanIf_RxNextIndex(uint8_t index)
{
    return (uint8_t)((index + 1u) % CANIF_RX_QUEUE_SIZE);
}

static uint8_t CanIf_TxNextIndex(uint8_t index)
{
    return (uint8_t)((index + 1u) % CANIF_TX_QUEUE_SIZE);
}

static CanIf_StatusType CanIf_ConfigureAcceptAllRxFilter(void)
{
    CAN_FilterTypeDef filter = {0};

    filter.FilterBank = 0u;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh = 0x0000u;
    filter.FilterIdLow = 0x0000u;
    filter.FilterMaskIdHigh = 0x0000u;
    filter.FilterMaskIdLow = 0x0000u;
    filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter.FilterActivation = ENABLE;
    filter.SlaveStartFilterBank = 14;

    if (HAL_CAN_ConfigFilter(s_canif_hcan, &filter) != HAL_OK)
    {
        s_canif_error_count++;
        return CANIF_ERR_HAL;
    }

    return CANIF_OK;
}

static CanIf_StatusType CanIf_ConfigureExactRxFilter(uint32_t filter_bank, uint32_t std_id)
{
    CAN_FilterTypeDef filter = {0};

    filter.FilterBank = filter_bank;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh = (uint16_t)(std_id << 5);
    filter.FilterIdLow = 0x0000u;
    /* Compare all 11 identifier bits. RTR/IDE remain don't-care. */
    filter.FilterMaskIdHigh = (uint16_t)(0x7FFu << 5);
    filter.FilterMaskIdLow = 0x0000u;
    filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter.FilterActivation = ENABLE;
    filter.SlaveStartFilterBank = 14;

    if (HAL_CAN_ConfigFilter(s_canif_hcan, &filter) != HAL_OK)
    {
        s_canif_error_count++;
        return CANIF_ERR_HAL;
    }

    return CANIF_OK;
}

void CanIf_Init(CAN_HandleTypeDef *hcan)
{
    s_canif_hcan = hcan;
    s_canif_tx_count = 0u;
    s_canif_rx_count = 0u;
    s_canif_rx_overflow_count = 0u;
    s_canif_error_count = 0u;
    s_canif_rx_head = 0u;
    s_canif_rx_tail = 0u;
    s_canif_tx_head = 0u;
    s_canif_tx_tail = 0u;
    s_canif_tx_queue_count = 0u;
    s_canif_tx_queue_high_water = 0u;
    s_canif_tx_queue_reject_count = 0u;
    s_canif_tx_queue_enqueue_count = 0u;
    s_canif_rx_queue_backend.push_from_isr = 0;
    s_canif_rx_queue_backend.pop = 0;
    s_canif_rx_queue_backend.context = 0;
    s_canif_rx_queue_backend_active = 0u;
}

CanIf_StatusType CanIf_SetRxQueueBackend(const CanIf_RxQueueBackendType *backend)
{
    if ((backend == 0) || (backend->push_from_isr == 0) || (backend->pop == 0))
    {
        return CANIF_ERR_PARAM;
    }

    s_canif_rx_queue_backend = *backend;
    s_canif_rx_queue_backend_active = 1u;
    return CANIF_OK;
}

CanIf_StatusType CanIf_Start(void)
{
    if (s_canif_hcan == 0)
    {
        return CANIF_ERR_PARAM;
    }

    /*
     * Temporary V3.1 bring-up policy: accept all at bxCAN, then retain the
     * established 0x601/0x7DF checks in the ISR and ECU application layer.
     * This makes a hardware-filter issue observable without changing UDS
     * access control. The exact-filter calls remain compiled for restoration
     * once board-level CAN RX validation is complete.
     */
    if (CanIf_ConfigureAcceptAllRxFilter() != CANIF_OK)
    {
        return CANIF_ERR_HAL;
    }
    if ((CANIF_USE_EXACT_RX_FILTER != 0u) &&
        ((CanIf_ConfigureExactRxFilter(0u, CANIF_ECU_REQUEST_ID) != CANIF_OK) ||
         (CanIf_ConfigureExactRxFilter(1u, CANIF_FUNCTIONAL_REQUEST_ID) != CANIF_OK)))
    {
        return CANIF_ERR_HAL;
    }

    if (HAL_CAN_Start(s_canif_hcan) != HAL_OK)
    {
        s_canif_error_count++;
        return CANIF_ERR_HAL;
    }

    if (HAL_CAN_ActivateNotification(s_canif_hcan, CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_ERROR | CAN_IT_BUSOFF) != HAL_OK)
    {
        s_canif_error_count++;
        return CANIF_ERR_HAL;
    }

    return CANIF_OK;
}

CanIf_StatusType CanIf_ReadRxFifo0(CAN_HandleTypeDef *hcan_arg, CanIf_FrameType *frame)
{
    CAN_RxHeaderTypeDef rx_header = {0};
    uint8_t i;

    if ((hcan_arg == 0) || (frame == 0) || (s_canif_hcan == 0))
    {
        return CANIF_ERR_PARAM;
    }
    if (hcan_arg->Instance != s_canif_hcan->Instance)
    {
        return CANIF_ERR_PARAM;
    }

    for (i = 0u; i < CANIF_CAN_DATA_LEN; i++)
    {
        frame->data[i] = 0u;
    }

    if (HAL_CAN_GetRxMessage(hcan_arg, CAN_RX_FIFO0, &rx_header, frame->data) != HAL_OK)
    {
        s_canif_error_count++;
        return CANIF_ERR_HAL;
    }

    if ((rx_header.IDE != CAN_ID_STD) || (rx_header.RTR != CAN_RTR_DATA))
    {
        return CANIF_ERR_HAL;
    }

    frame->id = rx_header.StdId;
    frame->dlc = (rx_header.DLC > CANIF_CAN_DATA_LEN) ? CANIF_CAN_DATA_LEN : (uint8_t)rx_header.DLC;

    return CANIF_OK;
}

CanIf_StatusType CanIf_RxIndication(const CanIf_FrameType *frame)
{
    uint8_t next_head;
    CanIf_StatusType status;

    if (frame == 0)
    {
        return CANIF_ERR_PARAM;
    }

    if (s_canif_rx_queue_backend_active != 0u)
    {
        status = s_canif_rx_queue_backend.push_from_isr(frame, s_canif_rx_queue_backend.context);
        if (status == CANIF_OK)
        {
            s_canif_rx_count++;
        }
        else if (status == CANIF_ERR_FULL)
        {
            s_canif_rx_overflow_count++;
        }
        return status;
    }

    next_head = CanIf_RxNextIndex(s_canif_rx_head);
    if (next_head == s_canif_rx_tail)
    {
        s_canif_rx_overflow_count++;
        return CANIF_ERR_FULL;
    }

    s_canif_rx_queue[s_canif_rx_head] = *frame;
    s_canif_rx_head = next_head;
    s_canif_rx_count++;
    return CANIF_OK;
}

CanIf_StatusType CanIf_Receive(CanIf_FrameType *frame)
{
    if (frame == 0)
    {
        return CANIF_ERR_PARAM;
    }

    if (s_canif_rx_queue_backend_active != 0u)
    {
        return s_canif_rx_queue_backend.pop(frame, s_canif_rx_queue_backend.context);
    }

    if (s_canif_rx_tail == s_canif_rx_head)
    {
        return CANIF_ERR_EMPTY;
    }

    *frame = s_canif_rx_queue[s_canif_rx_tail];
    s_canif_rx_tail = CanIf_RxNextIndex(s_canif_rx_tail);
    return CANIF_OK;
}

CanIf_StatusType CanIf_Transmit(uint32_t std_id, const uint8_t *data, uint8_t dlc)
{
    CanIf_FrameType *frame;

    if ((s_canif_hcan == 0) || (data == 0) || (dlc > CANIF_CAN_DATA_LEN) || (std_id > 0x7FFu))
    {
        return CANIF_ERR_PARAM;
    }

    if (s_canif_tx_queue_count >= CANIF_TX_QUEUE_SIZE)
    {
        s_canif_tx_queue_reject_count++;
        return CANIF_ERR_FULL;
    }

    frame = &s_canif_tx_queue[s_canif_tx_head];
    frame->id = std_id;
    frame->dlc = dlc;
    for (uint8_t index = 0u; index < CANIF_CAN_DATA_LEN; index++)
    {
        frame->data[index] = (index < dlc) ? data[index] : 0u;
    }
    s_canif_tx_head = CanIf_TxNextIndex(s_canif_tx_head);
    s_canif_tx_queue_count++;
    s_canif_tx_queue_enqueue_count++;
    if (s_canif_tx_queue_count > s_canif_tx_queue_high_water)
    {
        s_canif_tx_queue_high_water = s_canif_tx_queue_count;
    }

    CanIf_MainFunction();
    return CANIF_OK;
}

void CanIf_MainFunction(void)
{
    CAN_TxHeaderTypeDef tx_header = {0};
    uint32_t tx_mailbox = 0u;

    if (s_canif_hcan == 0)
    {
        return;
    }

    while (s_canif_tx_queue_count > 0u)
    {
        const CanIf_FrameType *frame = &s_canif_tx_queue[s_canif_tx_tail];

        tx_header.StdId = frame->id;
        tx_header.ExtId = 0u;
        tx_header.IDE = CAN_ID_STD;
        tx_header.RTR = CAN_RTR_DATA;
        tx_header.DLC = frame->dlc;
        tx_header.TransmitGlobalTime = DISABLE;

        if (HAL_CAN_AddTxMessage(s_canif_hcan, &tx_header, (uint8_t *)frame->data, &tx_mailbox) != HAL_OK)
        {
            break;
        }

        s_canif_tx_tail = CanIf_TxNextIndex(s_canif_tx_tail);
        s_canif_tx_queue_count--;
        s_canif_tx_count++;
    }
}

void CanIf_OnError(CAN_HandleTypeDef *hcan_arg)
{
    if ((hcan_arg != 0) && (s_canif_hcan != 0) && (hcan_arg->Instance == s_canif_hcan->Instance))
    {
        s_canif_error_count++;
    }
}

uint32_t CanIf_GetTxCount(void)
{
    return s_canif_tx_count;
}

uint32_t CanIf_GetTxQueueEnqueueCount(void)
{
    return s_canif_tx_queue_enqueue_count;
}

uint16_t CanIf_GetTxQueueRejectCount(void)
{
    return s_canif_tx_queue_reject_count;
}

uint8_t CanIf_GetTxQueuePendingCount(void)
{
    return s_canif_tx_queue_count;
}

uint8_t CanIf_GetTxQueueHighWaterMark(void)
{
    return s_canif_tx_queue_high_water;
}

uint32_t CanIf_GetRxCount(void)
{
    return s_canif_rx_count;
}

uint32_t CanIf_GetRxOverflowCount(void)
{
    return s_canif_rx_overflow_count;
}

uint32_t CanIf_GetErrorCount(void)
{
    return s_canif_error_count;
}
