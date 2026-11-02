/*
 * This file is not meant to be compiled directly.
 * Copy the snippets into the USER CODE sections of CubeIDE-generated main.c.
 */

/* USER CODE BEGIN Includes */
#include "minidcm_app.h"
/* USER CODE END Includes */

/* USER CODE BEGIN PV */
static uint8_t g_uart1_rx_byte;
static uint32_t g_led_last_tick;
/* USER CODE END PV */

/* USER CODE BEGIN 2 */
Minidcm_AppInit(&huart1);
HAL_UART_Receive_IT(&huart1, &g_uart1_rx_byte, 1);
/* USER CODE END 2 */

/* USER CODE BEGIN WHILE */
while (1)
{
  Minidcm_AppPoll();

  if ((HAL_GetTick() - g_led_last_tick) >= 500u) {
    g_led_last_tick = HAL_GetTick();
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
  }
/* USER CODE END WHILE */

/* USER CODE BEGIN 3 */
}
/* USER CODE END 3 */

/* USER CODE BEGIN 4 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1) {
    Minidcm_AppOnRxByte(g_uart1_rx_byte);
    HAL_UART_Receive_IT(&huart1, &g_uart1_rx_byte, 1);
  }
}
/* USER CODE END 4 */
