# Tester 固件迁移到 STM32CubeIDE 的计划

## 目标

把 Tester 固件从 Keil 教程式串口 echo 代码迁移到 STM32CubeIDE 工程，并接入 Mini-DCM 自己的 UART 协议层。

公共协议代码在这里：

```text
firmware/common/inc/minidcm_uart_link.h
firmware/common/src/minidcm_uart_link.c
```

这部分代码不依赖 Keil、不依赖 SPL、不依赖 HAL，也不依赖 CubeIDE。CubeIDE 工程只负责初始化串口、收发字节和调用协议层。

## CubeIDE 工程建议

第一版建议：

- MCU：STM32F103C8T6
- USART：USART1
- 波特率：9600 8N1
- TX：PA9
- RX：PA10
- LED：PC13 心跳灯

## 代码分层

建议结构：

```text
Core/Src/main.c
Core/Src/usart.c
Core/Src/minidcm_app.c
Core/Inc/minidcm_app.h
firmware/common/src/minidcm_uart_link.c
firmware/common/inc/minidcm_uart_link.h
```

职责划分：

| 层 | 职责 |
| --- | --- |
| `usart.c` | CubeMX 生成，负责 HAL UART 初始化 |
| `minidcm_uart_link.c` | 帧构造、帧解析、CRC16 |
| `minidcm_app.c` | 命令分发：PING、GET_INFO，后续 CAN/UDS |
| `main.c` | 初始化、心跳灯、轮询协议处理 |

## 第一版接收方式

第一版用 1 字节中断接收。它不是最高性能方案，但最容易调试。

后面通信量变大之后，再升级成 DMA + IDLE line。

基本流程：

```c
static uint8_t uart_rx_byte;

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART1_UART_Init();

    Minidcm_AppInit(&huart1);
    HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1);

    while (1) {
        Minidcm_AppPoll();
        HeartbeatPoll();
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        Minidcm_AppOnRxByte(uart_rx_byte);
        HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1);
    }
}
```

## 第一版命令

先只实现：

- `0x01 PING`：返回 `0x81`，seq 相同，payload 原样返回
- `0x02 GET_INFO`：返回 `0x82`，seq 相同，payload 是设备信息字符串

伪代码：

```c
void Minidcm_AppPoll(void)
{
    while (Ring_HasData()) {
        uint8_t byte = Ring_Pop();
        AppendToParseBuffer(byte);

        status = Minidcm_UartParseFrame(parse_buffer, parse_len, &frame, &consumed);
        DropConsumedBytes(consumed);

        if (status == MINIDCM_UART_OK) {
            DispatchFrame(&frame);
        }
    }
}
```

响应构造：

```c
uint8_t tx[MINIDCM_UART_MAX_FRAME_SIZE];
uint16_t tx_len;

Minidcm_UartBuildFrame(
    frame->seq,
    Minidcm_UartResponseCmd(frame->cmd),
    response_payload,
    response_len,
    tx,
    sizeof(tx),
    &tx_len);

HAL_UART_Transmit(&huart1, tx, tx_len, 100);
```

## PC 验证命令

刷入 CubeIDE 固件后，运行：

```powershell
python .\tools\pc_diag\serial_bridge.py ping --port COM9 --baudrate 9600 --payload hello --timeout-ms 1000
```

期望：

```json
{
  "ok": true,
  "responseCmd": "0x81",
  "rxPayloadText": "hello"
}
```

再运行：

```powershell
python .\tools\pc_diag\serial_bridge.py info --port COM9 --baudrate 9600 --timeout-ms 1000
```

期望返回 `0x82`，payload 是设备信息字符串。

## 不要继续沿用的写法

不要把教程代码继续扩展成正式工程：

- 不要只用一个全局 `Serial_RxData` 保存接收数据
- 不要在 `main` 里用很长的空循环 delay 阻塞
- 不要把直接字符串 echo 当协议
- 不要在 UART 中断里解析完整协议
- 不要让协议代码绑定 Keil / SPL

正确方向是：中断只收字节，主循环解析协议，协议代码独立可测试。
