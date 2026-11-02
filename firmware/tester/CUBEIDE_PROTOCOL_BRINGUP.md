# CubeIDE 接入 Mini-DCM 串口协议说明

## 当前目标

现在不要继续扩展 Keil 教程里的字符串 echo 代码，而是把 STM32CubeIDE 工程接入 Mini-DCM 自己的二进制 UART 协议。

第一版固件目标：

- 板子：STM32F103C8T6 最小系统板
- IDE：STM32CubeIDE
- 串口：USART1，9600 8N1
- 引脚：
  - PA9：USART1_TX
  - PA10：USART1_RX
  - PC13：LED 心跳
- 先实现两个命令：
  - `PING`：PC 发什么 payload，STM32 就按协议帧回什么 payload
  - `GET_INFO`：STM32 返回一段设备信息字符串

## 要加到 CubeIDE 工程里的文件

把下面两个应用层文件复制到你的 CubeIDE Tester 工程里：

```text
firmware/tester/cubeide_app/Core/Inc/minidcm_app.h
firmware/tester/cubeide_app/Core/Src/minidcm_app.c
```

还要把公共协议编解码文件也加进去：

```text
firmware/common/inc/minidcm_uart_link.h
firmware/common/src/minidcm_uart_link.c
```

第一版最省事的做法是直接复制：

```text
minidcm_uart_link.h -> CubeIDE工程/Core/Inc/
minidcm_uart_link.c -> CubeIDE工程/Core/Src/
minidcm_app.h       -> CubeIDE工程/Core/Inc/
minidcm_app.c       -> CubeIDE工程/Core/Src/
```

复制后，CubeIDE 工程结构大概应该是：

```text
minidcm_tester_f103/
  Core/
    Inc/
      main.h
      usart.h
      gpio.h
      minidcm_app.h
      minidcm_uart_link.h
    Src/
      main.c
      usart.c
      gpio.c
      minidcm_app.c
      minidcm_uart_link.c
```

后面工程稳定后，可以再改成从仓库的 `firmware/common` 目录引用公共源码。现在先复制进去，最容易成功。

## CubeMX 配置

在 CubeIDE 里面打开 `.ioc`，确认下面这些配置。

### SYS

```text
Debug: Serial Wire
```

这个很重要。不选 `Serial Wire` 的话，有时下载之后 ST-LINK 会连不上。

### USART1

```text
Mode: Asynchronous
Baud rate: 9600
Word length: 8 Bits
Parity: None
Stop bits: 1
Hardware flow control: None
```

引脚应该是：

```text
PA9  USART1_TX
PA10 USART1_RX
```

### GPIO

```text
PC13 GPIO_Output
```

很多 F103 最小系统板的 PC13 LED 是低电平点亮，所以亮灭逻辑反一点没关系。这里用它做心跳灯，只要能闪就行。

## Include Path

如果你把 `minidcm_uart_link.h` 复制到了 `Core/Inc`，就不需要额外配置 Include Path。

如果你想直接引用仓库里的公共头文件，就要在 CubeIDE 里加 include path，例如：

```text
../firmware/common/inc
```

具体相对路径取决于你的 CubeIDE 工程放在哪里。初次接入建议先复制到 `Core/Inc`，少踩坑。

## 修改 main.c

参考这个文件：

```text
firmware/tester/cubeide_app/Core/Src/main_user_snippets.c
```

注意：只把代码放进 CubeIDE 自动生成的 `USER CODE` 区域。否则下次重新生成代码时可能被覆盖。

### USER CODE BEGIN Includes

加：

```c
#include "minidcm_app.h"
```

### USER CODE BEGIN PV

加：

```c
static uint8_t g_uart1_rx_byte;
static uint32_t g_led_last_tick;
```

### USER CODE BEGIN 2

放在 `MX_USART1_UART_Init();` 后面：

```c
Minidcm_AppInit(&huart1);
HAL_UART_Receive_IT(&huart1, &g_uart1_rx_byte, 1);
```

作用：

- `Minidcm_AppInit(&huart1)`：把 USART1 交给 Mini-DCM 协议应用层
- `HAL_UART_Receive_IT(...)`：启动 1 字节中断接收

### while (1) 里面

在主循环里加：

```c
Minidcm_AppPoll();

if ((HAL_GetTick() - g_led_last_tick) >= 500u) {
  g_led_last_tick = HAL_GetTick();
  HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
}
```

作用：

- `Minidcm_AppPoll()`：从接收缓冲区取字节，解析 Mini-DCM 协议帧，处理 `PING` / `GET_INFO`
- 后面的 LED 逻辑只是心跳灯，证明主循环没有卡死

### USER CODE BEGIN 4

加 UART 接收完成回调：

```c
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1) {
    Minidcm_AppOnRxByte(g_uart1_rx_byte);
    HAL_UART_Receive_IT(&huart1, &g_uart1_rx_byte, 1);
  }
}
```

这里有一个关键点：**每次收到 1 个字节后，必须再次调用 `HAL_UART_Receive_IT`。**

否则只能收到第一个字节，后面的数据就不会进中断了。

## 代码运行逻辑

整体流程是：

```text
PC 发协议帧
  -> CH340
  -> STM32 PA10 / USART1_RX
  -> HAL_UART_RxCpltCallback 收到 1 字节
  -> Minidcm_AppOnRxByte 放入环形缓冲区
  -> main while 调用 Minidcm_AppPoll
  -> 协议解析成功
  -> 根据 CMD 处理 PING / GET_INFO
  -> HAL_UART_Transmit 从 PA9 发回响应帧
  -> CH340
  -> PC Python 工具解析响应
```

中断里只收字节，不解析协议。这是故意的。协议解析放在主循环里，后面更容易扩展。

## PC 端验证

编译、下载 CubeIDE 固件后，先测 `PING`：

```powershell
python .\tools\pc_diag\serial_bridge.py ping --port COM9 --baudrate 9600 --payload hello --timeout-ms 1000
```

期望看到：

```json
{
  "ok": true,
  "responseCmd": "0x81",
  "rxPayloadText": "hello"
}
```

再测 `GET_INFO`：

```powershell
python .\tools\pc_diag\serial_bridge.py info --port COM9 --baudrate 9600 --timeout-ms 1000
```

期望看到：

```json
{
  "ok": true,
  "responseCmd": "0x82",
  "rxPayloadText": "Mini-DCM Tester F103 USART1"
}
```

如果这两个都通，说明：

- CubeIDE 工程能正常控制板子
- USART1 TX/RX 都正常
- Mini-DCM UART 协议帧能正常收发
- 后续可以开始做 CAN/UDS 命令扩展

## 常见问题

- 忘了加 `HAL_UART_RxCpltCallback`
- `HAL_UART_RxCpltCallback` 没放到 `USER CODE` 区域
- 回调里忘了再次调用 `HAL_UART_Receive_IT`
- `minidcm_uart_link.h` 没放进 `Core/Inc`
- `minidcm_uart_link.c` 没放进 `Core/Src`
- `minidcm_app.c` 没参与编译
- PA9 / PA10 又接反了
- CH340 和 STM32 没有共地
- 串口助手或别的软件占用了 `COM9`
- CubeMX 重新生成代码时，把非 USER CODE 区域里的手写代码覆盖了
