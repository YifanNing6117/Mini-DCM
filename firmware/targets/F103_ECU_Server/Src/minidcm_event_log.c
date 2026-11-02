#include "minidcm_event_log.h"

static MiniDcm_EventLogEntryType s_event_log_buffer[MINIDCM_EVENT_LOG_CAPACITY];
static uint16_t s_event_log_write_index;
static uint16_t s_event_log_count;
static uint16_t s_event_log_next_sequence;
static uint16_t s_event_log_overflow_count;

void MiniDcm_EventLog_Init(void)
{
    uint16_t index;

    for (index = 0u; index < MINIDCM_EVENT_LOG_CAPACITY; index++)
    {
        s_event_log_buffer[index].sequence = 0u;
        s_event_log_buffer[index].event_id = MINIDCM_EVENT_NONE;
        s_event_log_buffer[index].detail = 0u;
        s_event_log_buffer[index].timestamp_ms = 0u;
    }

    s_event_log_write_index = 0u;
    s_event_log_count = 0u;
    s_event_log_next_sequence = 1u;
    s_event_log_overflow_count = 0u;
}

void MiniDcm_EventLog_Record(uint8_t event_id, uint16_t detail)
{
    MiniDcm_EventLog_RecordWithTimestamp(event_id, detail, 0u);
}

void MiniDcm_EventLog_RecordWithTimestamp(uint8_t event_id, uint16_t detail, uint32_t timestamp_ms)
{
    if (event_id == MINIDCM_EVENT_NONE)
    {
        return;
    }

    s_event_log_buffer[s_event_log_write_index].sequence = s_event_log_next_sequence;
    s_event_log_buffer[s_event_log_write_index].event_id = event_id;
    s_event_log_buffer[s_event_log_write_index].detail = detail;
    s_event_log_buffer[s_event_log_write_index].timestamp_ms = timestamp_ms;

    s_event_log_next_sequence++;
    if (s_event_log_next_sequence == 0u)
    {
        s_event_log_next_sequence = 1u;
    }

    s_event_log_write_index++;
    if (s_event_log_write_index >= MINIDCM_EVENT_LOG_CAPACITY)
    {
        s_event_log_write_index = 0u;
    }

    if (s_event_log_count < MINIDCM_EVENT_LOG_CAPACITY)
    {
        s_event_log_count++;
    }
    else
    {
        s_event_log_overflow_count++;
    }
}

MiniDcm_BoolType MiniDcm_EventLog_GetLatest(MiniDcm_EventLogEntryType *entry)
{
    uint16_t latest_index;

    if ((entry == 0) || (s_event_log_count == 0u))
    {
        return MINIDCM_FALSE;
    }

    if (s_event_log_write_index == 0u)
    {
        latest_index = MINIDCM_EVENT_LOG_CAPACITY - 1u;
    }
    else
    {
        latest_index = s_event_log_write_index - 1u;
    }

    *entry = s_event_log_buffer[latest_index];
    return MINIDCM_TRUE;
}

uint16_t MiniDcm_EventLog_GetCount(void)
{
    return s_event_log_count;
}

uint16_t MiniDcm_EventLog_GetOverflowCount(void)
{
    return s_event_log_overflow_count;
}

void MiniDcm_EventLog_Clear(void)
{
    MiniDcm_EventLog_Init();
}
