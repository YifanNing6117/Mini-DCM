#include "minidcm_task_profile.h"

static const MiniDcm_TaskProfileType s_task_profiles[MINIDCM_TASK_COUNT] =
{
    {
        MINIDCM_TASK_DCM,
        "Task_Dcm",
        1u,
        1u,
        50u,
        512u,
        MINIDCM_TASK_PRIORITY_HIGH,
        MINIDCM_TASK_TRIGGER_EVENT_OR_PERIODIC
    },
    {
        MINIDCM_TASK_MONITOR,
        "Task_Monitor",
        100u,
        100u,
        300u,
        256u,
        MINIDCM_TASK_PRIORITY_NORMAL,
        MINIDCM_TASK_TRIGGER_PERIODIC
    },
    {
        MINIDCM_TASK_HEARTBEAT,
        "Task_Heartbeat",
        500u,
        500u,
        1500u,
        128u,
        MINIDCM_TASK_PRIORITY_LOW,
        MINIDCM_TASK_TRIGGER_PERIODIC
    }
};

const MiniDcm_TaskProfileType *MiniDcm_TaskProfile_GetAll(uint8_t *count)
{
    if (count != 0)
    {
        *count = (uint8_t)MINIDCM_TASK_COUNT;
    }

    return s_task_profiles;
}

const MiniDcm_TaskProfileType *MiniDcm_TaskProfile_Get(MiniDcm_TaskIdType id)
{
    if ((uint8_t)id >= (uint8_t)MINIDCM_TASK_COUNT)
    {
        return 0;
    }

    return &s_task_profiles[(uint8_t)id];
}
