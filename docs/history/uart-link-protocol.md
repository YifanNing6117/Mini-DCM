> 历史设计记录：原开发目录中的路径、阶段目标与实验记录按当时状态保留。当前构建、运行和验证入口见[仓库 README](../../README.md)。部分原始日志与规划资料未纳入公开快照。

# Mini-DCM UART Link Protocol

## Purpose

This protocol defines the PC-to-STM32 control channel used by the Mini-DCM workbench and Tester firmware.

The UART link is not a tutorial echo channel. It is the first stable transport layer for later CAN and UDS workflows:

- PC workbench sends commands to the Tester node.
- Tester node returns command responses, status, logs, and future CAN/UDS data.
- Firmware can be moved from Keil to STM32CubeIDE without changing the protocol logic.

## Physical Layer

- UART: 9600 8N1 for current bring-up
- PC adapter: CH340 on `COM9`
- STM32F103 USART1:
  - `PA9` -> CH340 `RXD`
  - `PA10` <- CH340 `TXD`
  - `GND` <-> `GND`

The protocol implementation should keep the baudrate configurable. Later iterations may use 115200 baud.

## Frame Format

All multi-byte fields are little-endian.

```text
+--------+---------+-----+-----+-----+---------+-------+
| SOF[2] | LEN[2]  | VER | SEQ | CMD | PAYLOAD | CRC16 |
+--------+---------+-----+-----+-----+---------+-------+
```

Field definitions:

| Field | Size | Description |
| --- | ---: | --- |
| `SOF` | 2 | Fixed bytes `0xA5 0x5A` |
| `LEN` | 2 | Number of bytes from `VER` through payload end: `3 + payload_len` |
| `VER` | 1 | Protocol version. Current version is `0x01` |
| `SEQ` | 1 | Sequence number selected by sender and echoed by responder |
| `CMD` | 1 | Command or response code |
| `PAYLOAD` | 0..N | Command-specific data |
| `CRC16` | 2 | CRC-16/CCITT-FALSE over `LEN`, `VER`, `SEQ`, `CMD`, and `PAYLOAD` |

CRC settings:

- Polynomial: `0x1021`
- Initial value: `0xFFFF`
- RefIn/RefOut: false
- XorOut: `0x0000`

## Resynchronization

The receiver scans for `0xA5 0x5A`. Any byte before a valid SOF is discarded.

A frame is discarded if:

- `LEN < 3`
- `LEN > MINIDCM_UART_MAX_BODY`
- version is unsupported
- CRC does not match

After a discard, the parser continues scanning the remaining stream. This makes the protocol recover from reset noise, partial reads, and terminal garbage.

## Command Map

Command codes below `0x80` are requests. Responses use `request_cmd | 0x80`.

| Request | Response | Name | Payload |
| ---: | ---: | --- | --- |
| `0x01` | `0x81` | `PING` | Request: optional bytes. Response: same bytes |
| `0x02` | `0x82` | `GET_INFO` | Response: UTF-8 device info string |
| `0x03` | `0x83` | `SET_MODE` | Request: one mode byte |
| `0x10` | `0x90` | `CAN_TX` | Future: CAN ID, flags, data |
| `0x11` | `0x91` | `CAN_RX_POLL` | Future: poll buffered CAN frames |
| `0x20` | `0xA0` | `UDS_REQUEST` | Future: target CAN ID and UDS payload |

Reserved response:

| Code | Name | Payload |
| ---: | --- | --- |
| `0xFF` | `ERROR` | `request_cmd`, `error_code`, optional detail bytes |

Error codes:

| Code | Meaning |
| ---: | --- |
| `0x01` | Unknown command |
| `0x02` | Invalid payload length |
| `0x03` | Unsupported protocol version |
| `0x04` | Busy |

## First Firmware Behavior

The first STM32CubeIDE firmware should implement:

1. UART interrupt or DMA RX byte collection.
2. Byte-stream parser using the shared C codec.
3. `PING`: reply with the same payload.
4. `GET_INFO`: reply with a short string, for example `Mini-DCM Tester F103 USART1`.
5. LED heartbeat independent from UART.

The main loop should not block in long delay loops. UART parsing must continue even while status LEDs blink.

## First PC Behavior

The PC tool should implement:

1. Build frames from command name, sequence, and payload.
2. Send raw frame bytes over serial.
3. Read bytes until one valid response frame is parsed or timeout expires.
4. Print JSON for the web-console backend.

## Iteration Rules

- Do not change existing command meanings once they are used by the workbench.
- Add new commands instead of overloading payloads.
- Keep the frame header stable.
- Keep parser code independent from STM32 HAL/SPL so it can be unit-tested on PC.
- Add tests for every new command payload format before binding it to Web UI controls.
