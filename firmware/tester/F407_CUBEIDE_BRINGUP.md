# STM32F407 Tester CubeIDE Bring-up

## 2026-06-24 Current Checkpoint

The F407 base bring-up is complete:

- Real CubeMX/CubeIDE project: `../Mini-DCM/firmware/targets/F407_Tester`
- MCU selected in CubeMX: `STM32F407VETx`
- SWD debug enabled through `SYS -> Debug -> Serial Wire`
- HSE/PLL configured for `168 MHz`
- External heartbeat LED: `PE3 -> 1 kOhm -> LED -> GND`
- Stable flashing workflow: build in CubeIDE, then download `../Mini-DCM/firmware/targets/F407_Tester\Debug\F407_Tester.elf` with STM32CubeProgrammer
- F407 USART1 raw echo is verified on `COM14`, `9600 8N1`
  - Startup: `F407 USART1 ready\r\n`
  - Echo: send `A`, receive `RX: A\r\n`

Next task: port the Mini-DCM UART protocol app to this F407 project and validate:

```text
PING     0x01 -> 0x81
GET_INFO 0x02 -> 0x82, payload = Mini-DCM Tester F407 USART1
```

当前目标是把已经在 F103 临时验证通过的 PC ↔ STM32 UART 协议，迁移到最终 Tester 节点 STM32F407VET6。

## 当前已完成

- Web / Python / 串口桥链路已验证。
- Mini-DCM UART 帧协议已验证：
  - `0x01 PING -> 0x81`
  - `0x02 GET_INFO -> 0x82`
- F103 临时板能通过 COM11、9600 8N1 返回协议响应。

## F407 第一阶段目标

先不要急着接 CAN。F407 先做到和 F103 当前能力等价：

```text
PC / Web
  -> CH340 / USB 串口
  -> F407 USART
  -> Mini-DCM UART frame parser
  -> PING / GET_INFO response
```

验收标准：

- CubeProgrammer 能识别 F407。
- CubeIDE 能编译、烧录 F407 工程。
- 板载 LED 或外接 LED 能心跳闪烁。
- Python 命令能收到：
  - PING payload 原样返回
  - GET_INFO 返回 `Mini-DCM Tester F407 USART1`

## CubeIDE 配置建议

具体引脚以实际 F407 开发板丝印为准。若无冲突，优先使用：

- USART1_TX: PA9
- USART1_RX: PA10
- Baudrate: 9600
- Word length: 8 bits
- Parity: None
- Stop bits: 1
- Hardware flow control: Disable
- USART global interrupt: Enable

LED 引脚不要猜，先看开发板原理图/丝印/示例工程。若不确定，可以先外接 LED 到一个普通 GPIO 验证。

## 代码移植注意

`minidcm_app.h` 使用：

```c
#include "main.h"
```

不要直接包含 `stm32f1xx_hal.h` 或 `stm32f4xx_hal.h`。原因是 CubeIDE 每个芯片工程都会生成自己的 `main.h`，并在里面包含正确的 HAL 家族头文件。这样同一份应用代码可以被 F103/F407 工程复用。

F407 Tester 默认设备信息字符串：

```c
Mini-DCM Tester F407 USART1
```

如果临时在别的板子验证，可在工程编译选项或头文件中定义 `MINIDCM_INFO_TEXT` 覆盖默认值。

## main.c USER CODE 接入点

在 `/* USER CODE BEGIN Includes */`：

```c
#include "minidcm_app.h"
```

在 `/* USER CODE BEGIN PV */`：

```c
static uint8_t g_uart1_rx_byte;
static uint32_t g_led_last_tick;
```

在 `/* USER CODE BEGIN 2 */`，所有 `MX_*_Init()` 完成之后：

```c
Minidcm_AppInit(&huart1);
HAL_UART_Receive_IT(&huart1, &g_uart1_rx_byte, 1u);
```

在 `while (1)` 的 USER CODE WHILE 区域：

```c
Minidcm_AppPoll();

uint32_t now = HAL_GetTick();
if ((now - g_led_last_tick) >= 500u) {
    g_led_last_tick = now;
    HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
}
```

`LED_GPIO_Port` 和 `LED_Pin` 要替换成 F407 工程实际生成的名字。

在 `/* USER CODE BEGIN 4 */`：

```c
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        Minidcm_AppOnRxByte(g_uart1_rx_byte);
        HAL_UART_Receive_IT(&huart1, &g_uart1_rx_byte, 1u);
    }
}
```

## 上位机验证命令

端口号以实际 CH340 为准：

```powershell
python .\tools\pc_diag\serial_bridge.py ping --port COM11 --baudrate 9600 --payload hello --timeout-ms 1500
python .\tools\pc_diag\serial_bridge.py info --port COM11 --baudrate 9600 --timeout-ms 1500
```

## 下一步

F407 UART 验证通过后，再让 F103 回到 ECU Server 角色，进入 F407 ↔ F103 的 CAN 500 Kbps 原始帧联调。
