/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "minidcm_osal.h"
#include "can_if.h"
#include "ecu_app.h"
#include "minidcm_error.h"
#include "minidcm_runtime_stats.h"
#include "minidcm_scheduler.h"
#include "queue.h"
#include "task.h"
#include "minidcm_task_runtime.h"
#include "mpu6050_hal_port.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
static uint32_t F103_GetTickMs(void);
static void F103_EnterCritical(void);
static void F103_ExitCritical(void);
static void Led_MainFunction(uint32_t now_ms);
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define ECU_SCHEDULER_TASK_COUNT 2u
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
CAN_HandleTypeDef hcan;

I2C_HandleTypeDef hi2c1;

osThreadId defaultTaskHandle;
/* USER CODE BEGIN PV */
osThreadId dcmTaskHandle;
osThreadId monitorTaskHandle;
osThreadId heartbeatTaskHandle;

static const MiniDcm_OsalConfigType g_osal_config =
{
    F103_GetTickMs,
    F103_EnterCritical,
    F103_ExitCritical,
    0
};

static MiniDcm_SchedulerType g_ecu_scheduler;
static MiniDcm_SchedulerTaskType g_ecu_scheduler_tasks[ECU_SCHEDULER_TASK_COUNT] =
{
    {EcuApp_MainFunction, 1u, 0u, TRUE},
    {Led_MainFunction, 500u, 0u, TRUE}
};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_CAN_Init(void);
static void MX_I2C1_Init(void);
void StartDefaultTask(void const * argument);

/* USER CODE BEGIN PFP */
void StartDcmTask(void const * argument);
void StartMonitorTask(void const * argument);
void StartHeartbeatTask(void const * argument);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static uint32_t F103_GetTickMs(void)
{
    return HAL_GetTick();
}

static void F103_EnterCritical(void)
{
    taskENTER_CRITICAL();
}

static void F103_ExitCritical(void)
{
    taskEXIT_CRITICAL();
}

static void Led_MainFunction(uint32_t now_ms)
{
    (void)now_ms;
    HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
}
static QueueHandle_t s_f103_can_rx_queue;

static CanIf_StatusType F103_CanRxQueuePushFromIsr(const CanIf_FrameType *frame, void *context)
{
    QueueHandle_t queue = (QueueHandle_t)context;

    if ((queue == NULL) || (frame == NULL))
    {
        return CANIF_ERR_PARAM;
    }

    if (xQueueSendFromISR(queue, frame, NULL) != pdPASS)
    {
        MiniDcm_TaskRuntime_OnCanRxQueuePushRejectedFromIsr();
        return CANIF_ERR_FULL;
    }

    MiniDcm_TaskRuntime_OnCanRxQueuePushFromIsr((uint32_t)uxQueueMessagesWaitingFromISR(queue));
    return CANIF_OK;
}

static CanIf_StatusType F103_CanRxQueuePop(CanIf_FrameType *frame, void *context)
{
    QueueHandle_t queue = (QueueHandle_t)context;

    if ((queue == NULL) || (frame == NULL))
    {
        return CANIF_ERR_PARAM;
    }

    return (xQueueReceive(queue, frame, 0u) == pdPASS) ? CANIF_OK : CANIF_ERR_EMPTY;
}

static CanIf_StatusType F103_ConfigureCanIfRxQueue(void)
{
    static const CanIf_RxQueueBackendType backend =
    {
        F103_CanRxQueuePushFromIsr,
        F103_CanRxQueuePop,
        0
    };
    CanIf_RxQueueBackendType configured_backend = backend;

    s_f103_can_rx_queue = xQueueCreate(16u, sizeof(CanIf_FrameType));
    if (s_f103_can_rx_queue == NULL)
    {
        return CANIF_ERR_FULL;
    }

    configured_backend.context = (void *)s_f103_can_rx_queue;
    return CanIf_SetRxQueueBackend(&configured_backend);
}





/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_CAN_Init();
  MX_I2C1_Init();
  F103_Mpu6050_RegisterSensorApp();
  /* USER CODE BEGIN 2 */
  MiniDcm_Error_Init();

  if (MiniDcm_Osal_Init(&g_osal_config) != E_OK)
  {
      Error_Handler();
  }
  MiniDcm_RuntimeStats_Init();

  CanIf_Init(&hcan);
  if (F103_ConfigureCanIfRxQueue() != CANIF_OK)
  {
      Error_Handler();
  }
  if (CanIf_Start() != CANIF_OK)
  {
      Error_Handler();
  }


  if (MiniDcm_Scheduler_InitWithOsal(&g_ecu_scheduler,
                                     g_ecu_scheduler_tasks,
                                     ECU_SCHEDULER_TASK_COUNT) != E_OK)
  {
      Error_Handler();
  }
  /* USER CODE END 2 */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* definition and creation of defaultTask */
  osThreadDef(defaultTask, StartDefaultTask, osPriorityNormal, 0, 128);
  defaultTaskHandle = osThreadCreate(osThread(defaultTask), NULL);

  /* USER CODE BEGIN RTOS_THREADS */
  osThreadDef(dcmTask, StartDcmTask, osPriorityAboveNormal, 0, 512);
  dcmTaskHandle = osThreadCreate(osThread(dcmTask), NULL);

  osThreadDef(monitorTask, StartMonitorTask, osPriorityNormal, 0, 256);
  monitorTaskHandle = osThreadCreate(osThread(monitorTask), NULL);

  osThreadDef(heartbeatTask, StartHeartbeatTask, osPriorityLow, 0, 128);
  heartbeatTaskHandle = osThreadCreate(osThread(heartbeatTask), NULL);

  if ((dcmTaskHandle == NULL) || (monitorTaskHandle == NULL) || (heartbeatTaskHandle == NULL))
  {
    Error_Handler();
  }
  /* USER CODE END RTOS_THREADS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */
  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
      MiniDcm_Scheduler_RunWithOsal(&g_ecu_scheduler);
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief CAN Initialization Function
  * @param None
  * @retval None
  */
static void MX_CAN_Init(void)
{

  /* USER CODE BEGIN CAN_Init 0 */

  /* USER CODE END CAN_Init 0 */

  /* USER CODE BEGIN CAN_Init 1 */

  /* USER CODE END CAN_Init 1 */
  hcan.Instance = CAN1;
  hcan.Init.Prescaler = 4;
  hcan.Init.Mode = CAN_MODE_NORMAL;
  hcan.Init.SyncJumpWidth = CAN_SJW_1TQ;
  hcan.Init.TimeSeg1 = CAN_BS1_13TQ;
  hcan.Init.TimeSeg2 = CAN_BS2_4TQ;
  hcan.Init.TimeTriggeredMode = DISABLE;
  hcan.Init.AutoBusOff = ENABLE;
  hcan.Init.AutoWakeUp = DISABLE;
  hcan.Init.AutoRetransmission = ENABLE;
  hcan.Init.ReceiveFifoLocked = DISABLE;
  hcan.Init.TransmitFifoPriority = DISABLE;
  if (HAL_CAN_Init(&hcan) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CAN_Init 2 */

  /* USER CODE END CAN_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : LED_Pin */
  GPIO_InitStruct.Pin = LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_GPIO_Port, &GPIO_InitStruct);

}

/* USER CODE BEGIN 4 */










static void MiniDcm_FreeRtosHeartbeatHook(void)
{
#if defined(LED_GPIO_Port) && defined(LED_Pin)
  HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
#endif
}

void StartDcmTask(void const * argument)
{
  (void)argument;

  for (;;)
  {
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1u)) > 0u)
    {
      MiniDcm_TaskRuntime_RecordDcmNotificationWakeup();
    }
    MiniDcm_TaskRuntime_DcmOnce();
    MiniDcm_TaskRuntime_UpdateDcmStackHighWaterMark((uint32_t)uxTaskGetStackHighWaterMark(NULL));
    osDelay(1);
  }
}

void StartMonitorTask(void const * argument)
{
  (void)argument;

  for (;;)
  {
    MiniDcm_TaskRuntime_MonitorOnce();
    MiniDcm_TaskRuntime_UpdateMonitorStackHighWaterMark((uint32_t)uxTaskGetStackHighWaterMark(NULL));
    osDelay(100);
  }
}

void StartHeartbeatTask(void const * argument)
{
  (void)argument;

  for (;;)
  {
    MiniDcm_TaskRuntime_HeartbeatOnce(MiniDcm_FreeRtosHeartbeatHook);

    MiniDcm_TaskRuntime_UpdateHeartbeatStackHighWaterMark((uint32_t)uxTaskGetStackHighWaterMark(NULL));
    osDelay(500);
  }
}
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan_arg)
{
    CanIf_FrameType rx_frame = {0};

    if (CanIf_ReadRxFifo0(hcan_arg, &rx_frame) != CANIF_OK)
    {
        return;
    }

    if ((rx_frame.id == CANIF_ECU_REQUEST_ID) || (rx_frame.id == CANIF_FUNCTIONAL_REQUEST_ID))
    {
        if (CanIf_RxIndication(&rx_frame) == CANIF_OK)
        {
            BaseType_t higher_priority_task_woken = pdFALSE;

            if (dcmTaskHandle != NULL)
            {
                MiniDcm_TaskRuntime_OnCanRxNotificationFromIsr();
                vTaskNotifyGiveFromISR((TaskHandle_t)dcmTaskHandle, &higher_priority_task_woken);
                portYIELD_FROM_ISR(higher_priority_task_woken);
            }
        }
    }
}

void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *hcan_arg)
{
    if (hcan_arg->Instance == CAN1)
    {
        CanIf_OnError(hcan_arg);
    }
}
/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void const * argument)
{
  /* USER CODE BEGIN 5 */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END 5 */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM1 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM1) {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */

  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     tex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
