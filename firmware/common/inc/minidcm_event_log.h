#ifndef MINIDCM_EVENT_LOG_H
#define MINIDCM_EVENT_LOG_H

#include "minidcm_types.h"

#define MINIDCM_EVENT_LOG_CAPACITY        16u

#define MINIDCM_EVENT_NONE                ((uint8_t)0x00u)
#define MINIDCM_EVENT_STARTUP             ((uint8_t)0x01u)
#define MINIDCM_EVENT_ERROR               ((uint8_t)0x02u)
#define MINIDCM_EVENT_CAN_RX              ((uint8_t)0x10u)
#define MINIDCM_EVENT_CAN_TX_ERROR        ((uint8_t)0x11u)
#define MINIDCM_EVENT_UDS_REQUEST         ((uint8_t)0x20u)
#define MINIDCM_EVENT_SECURITY_UNLOCKED   ((uint8_t)0x30u)
#define MINIDCM_EVENT_SECURITY_LOCKED     ((uint8_t)0x31u)

typedef struct
{
    uint16_t sequence;
    uint8_t event_id;
    uint16_t detail;
    uint32_t timestamp_ms;
} MiniDcm_EventLogEntryType;

void MiniDcm_EventLog_Init(void);
void MiniDcm_EventLog_Record(uint8_t event_id, uint16_t detail);
void MiniDcm_EventLog_RecordWithTimestamp(uint8_t event_id, uint16_t detail, uint32_t timestamp_ms);
MiniDcm_BoolType MiniDcm_EventLog_GetLatest(MiniDcm_EventLogEntryType *entry);
uint16_t MiniDcm_EventLog_GetCount(void);
uint16_t MiniDcm_EventLog_GetOverflowCount(void);
void MiniDcm_EventLog_Clear(void);

#endif
