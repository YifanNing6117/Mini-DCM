#ifndef MINIDCM_SCHEDULER_H
#define MINIDCM_SCHEDULER_H

#include "minidcm_types.h"

typedef void (*MiniDcm_TaskFunctionType)(uint32_t now_ms);

typedef struct
{
    MiniDcm_TaskFunctionType function;
    uint32_t period_ms;
    uint32_t last_run_ms;
    uint8_t enabled;
} MiniDcm_SchedulerTaskType;

typedef struct
{
    MiniDcm_SchedulerTaskType *tasks;
    uint8_t task_count;
    uint8_t initialized;
} MiniDcm_SchedulerType;

Std_ReturnType MiniDcm_Scheduler_Init(MiniDcm_SchedulerType *scheduler,
                                      MiniDcm_SchedulerTaskType *tasks,
                                      uint8_t task_count,
                                      uint32_t now_ms);

Std_ReturnType MiniDcm_Scheduler_InitWithOsal(MiniDcm_SchedulerType *scheduler,
                                              MiniDcm_SchedulerTaskType *tasks,
                                              uint8_t task_count);

void MiniDcm_Scheduler_Run(MiniDcm_SchedulerType *scheduler, uint32_t now_ms);
void MiniDcm_Scheduler_RunWithOsal(MiniDcm_SchedulerType *scheduler);
Std_ReturnType MiniDcm_Scheduler_SetEnabled(MiniDcm_SchedulerType *scheduler,
                                            uint8_t task_index,
                                            uint8_t enabled);

#endif
