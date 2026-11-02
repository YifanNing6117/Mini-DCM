#ifndef CAN_IF_H
#define CAN_IF_H

#include <stdint.h>
#include "main.h"

#define CANIF_CAN_DATA_LEN 8u
#define CANIF_TX_QUEUE_SIZE 16u
#define CANIF_ECU_REQUEST_ID 0x601u
#define CANIF_FUNCTIONAL_REQUEST_ID 0x7DFu
#define CANIF_ECU_RESPONSE_ID 0x602u

typedef enum
{
    CANIF_OK = 0u,
    CANIF_ERR_PARAM,
    CANIF_ERR_HAL,
    CANIF_ERR_FULL,
    CANIF_ERR_EMPTY
} CanIf_StatusType;

typedef struct
{
    uint32_t id;
    uint8_t dlc;
    uint8_t data[CANIF_CAN_DATA_LEN];
} CanIf_FrameType;

typedef CanIf_StatusType (*CanIf_RxQueuePushFromIsrFuncType)(const CanIf_FrameType *frame, void *context);
typedef CanIf_StatusType (*CanIf_RxQueuePopFuncType)(CanIf_FrameType *frame, void *context);

typedef struct
{
    CanIf_RxQueuePushFromIsrFuncType push_from_isr;
    CanIf_RxQueuePopFuncType pop;
    void *context;
} CanIf_RxQueueBackendType;

void CanIf_Init(CAN_HandleTypeDef *hcan);
CanIf_StatusType CanIf_SetRxQueueBackend(const CanIf_RxQueueBackendType *backend);
CanIf_StatusType CanIf_Start(void);
CanIf_StatusType CanIf_ReadRxFifo0(CAN_HandleTypeDef *hcan_arg, CanIf_FrameType *frame);
CanIf_StatusType CanIf_RxIndication(const CanIf_FrameType *frame);
CanIf_StatusType CanIf_Receive(CanIf_FrameType *frame);
CanIf_StatusType CanIf_Transmit(uint32_t std_id, const uint8_t *data, uint8_t dlc);
void CanIf_MainFunction(void);
void CanIf_OnError(CAN_HandleTypeDef *hcan_arg);
uint32_t CanIf_GetTxCount(void);
uint32_t CanIf_GetTxQueueEnqueueCount(void);
uint16_t CanIf_GetTxQueueRejectCount(void);
uint8_t CanIf_GetTxQueuePendingCount(void);
uint8_t CanIf_GetTxQueueHighWaterMark(void);
uint32_t CanIf_GetRxCount(void);
uint32_t CanIf_GetRxOverflowCount(void);
uint32_t CanIf_GetErrorCount(void);

#endif
