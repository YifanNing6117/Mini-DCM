#ifndef MINIDCM_TASK_PROFILE_H
#define MINIDCM_TASK_PROFILE_H

#include "minidcm_types.h"

typedef enum
{
    MINIDCM_TASK_DCM = 0u,
    MINIDCM_TASK_MONITOR,
    MINIDCM_TASK_HEARTBEAT,
    MINIDCM_TASK_COUNT
} MiniDcm_TaskIdType;

typedef enum
{
    MINIDCM_TASK_TRIGGER_PERIODIC = 0u,
    MINIDCM_TASK_TRIGGER_EVENT_OR_PERIODIC
} MiniDcm_TaskTriggerType;

typedef enum
{
    MINIDCM_TASK_PRIORITY_LOW = 0u,
    MINIDCM_TASK_PRIORITY_NORMAL,
    MINIDCM_TASK_PRIORITY_HIGH
} MiniDcm_TaskPriorityClassType;

typedef struct
{
    MiniDcm_TaskIdType id;
    const char *name;
    uint32_t baremetal_period_ms;
    uint32_t rtos_period_ms;
    uint32_t health_timeout_ms;
    uint16_t suggested_stack_words;
    MiniDcm_TaskPriorityClassType priority_class;
    MiniDcm_TaskTriggerType trigger_type;
} MiniDcm_TaskProfileType;

const MiniDcm_TaskProfileType *MiniDcm_TaskProfile_GetAll(uint8_t *count);
const MiniDcm_TaskProfileType *MiniDcm_TaskProfile_Get(MiniDcm_TaskIdType id);

#endif
