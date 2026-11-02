#include "minidcm_scheduler.h"

#include "minidcm_error.h"
#include "minidcm_osal.h"

Std_ReturnType MiniDcm_Scheduler_Init(MiniDcm_SchedulerType *scheduler,
                                      MiniDcm_SchedulerTaskType *tasks,
                                      uint8_t task_count,
                                      uint32_t now_ms)
{
    uint8_t i;

    if ((scheduler == NULL) || (tasks == NULL) || (task_count == 0u))
    {
        MiniDcm_Error_Report(MINIDCM_E_PARAM);
        return E_NOT_OK;
    }

    scheduler->tasks = tasks;
    scheduler->task_count = task_count;
    scheduler->initialized = TRUE;

    for (i = 0u; i < task_count; i++)
    {
        scheduler->tasks[i].last_run_ms = now_ms;
    }

    return E_OK;
}

Std_ReturnType MiniDcm_Scheduler_InitWithOsal(MiniDcm_SchedulerType *scheduler,
                                              MiniDcm_SchedulerTaskType *tasks,
                                              uint8_t task_count)
{
    if (MiniDcm_Osal_IsInitialized() == FALSE)
    {
        MiniDcm_Error_Report(MINIDCM_E_NOT_INIT);
        return E_NOT_OK;
    }

    return MiniDcm_Scheduler_Init(scheduler, tasks, task_count, MiniDcm_Osal_GetTickMs());
}

void MiniDcm_Scheduler_Run(MiniDcm_SchedulerType *scheduler, uint32_t now_ms)
{
    uint8_t i;

    if ((scheduler == NULL) || (scheduler->initialized == FALSE))
    {
        MiniDcm_Error_Report(MINIDCM_E_NOT_INIT);
        return;
    }

    for (i = 0u; i < scheduler->task_count; i++)
    {
        MiniDcm_SchedulerTaskType *task = &scheduler->tasks[i];

        if ((task->enabled == FALSE) || (task->function == NULL) || (task->period_ms == 0u))
        {
            continue;
        }

        if ((uint32_t)(now_ms - task->last_run_ms) >= task->period_ms)
        {
            task->last_run_ms = now_ms;
            task->function(now_ms);
        }
    }
}

void MiniDcm_Scheduler_RunWithOsal(MiniDcm_SchedulerType *scheduler)
{
    if (MiniDcm_Osal_IsInitialized() == FALSE)
    {
        MiniDcm_Error_Report(MINIDCM_E_NOT_INIT);
        return;
    }

    MiniDcm_Scheduler_Run(scheduler, MiniDcm_Osal_GetTickMs());
}

Std_ReturnType MiniDcm_Scheduler_SetEnabled(MiniDcm_SchedulerType *scheduler,
                                            uint8_t task_index,
                                            uint8_t enabled)
{
    if ((scheduler == NULL) || (scheduler->initialized == FALSE))
    {
        MiniDcm_Error_Report(MINIDCM_E_NOT_INIT);
        return E_NOT_OK;
    }

    if (task_index >= scheduler->task_count)
    {
        MiniDcm_Error_Report(MINIDCM_E_PARAM);
        return E_NOT_OK;
    }

    scheduler->tasks[task_index].enabled = (enabled != FALSE) ? TRUE : FALSE;
    return E_OK;
}
