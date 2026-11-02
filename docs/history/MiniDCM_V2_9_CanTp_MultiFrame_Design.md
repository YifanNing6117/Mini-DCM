> 历史设计记录：原开发目录中的路径、阶段目标与实验记录按当时状态保留。当前构建、运行和验证入口见[仓库 README](../../README.md)。部分原始日志与规划资料未纳入公开快照。

# MiniDCM V2.9：CAN-TP 多帧传输设计

日期：2026-07-12  
状态：设计冻结，尚未开始修改 ECU/F407 硬件协议路径  
前置基线：V2.7 / V2.8 双侧 CAN 有界队列已通过真实硬件验收

## 1. 为什么现在进入多帧

V2.8 已经解决了突发 CAN 请求时的两个工程问题：

```text
F407 Tester：CAN TX FIFO + CAN RX FIFO，容量均为 8 帧
F103 ECU   ：CanIf TX FIFO，容量为 16 帧
```

8 帧 `0x601` 突发读取验证的真实结果为：Tester 成功接受 8 帧请求，ECU 依次返回 8 帧 `0x602` 响应，Tester `can-drain` 依次取出 8 帧。这说明后续 CAN-TP 的 FF/FC/CF 序列有稳定的帧级缓冲边界，不再建立在“直接塞 bxCAN mailbox”的脆弱路径上。

当前传输层仍只支持 CAN-TP Single Frame：

```text
CanTp_DecodeSingleFrame() / CanTp_EncodeSingleFrame()
最大 UDS payload：7 字节
UDS_SERVER_MAX_RESPONSE_LEN：7 字节
```

因此，V2.9 的目标不是泛泛地“增加一个长响应”，而是把协议边界从 Single Frame 扩展为可验证的 ISO 15765-2 FF / FC / CF 状态机。

## 2. 范围与非范围

### 本阶段要实现

- 11 bit 标准 CAN ID 下的 CAN-TP 多帧基础流程；
- First Frame（FF）、Flow Control（FC）、Consecutive Frame（CF）；
- ECU 接收多帧请求、ECU 发送多帧响应；
- N_Bs / N_Cr 超时与 Sequence Number 校验；
- 静态传输缓冲区，默认最多 256 字节；
- 保持现有单帧 UDS 回归 16/16 不变；
- 用一个明确的长 DID 作为硬件验收载荷，而不是伪造大报文。

### 本阶段不做

- ISO-TP 扩展长度、CAN FD、29 bit ID；
- 多会话并发、多连接寻址；
- Block Size 节流的完整压力策略；
- 动态内存分配；
- 将未完成的传输功能包装成“完整 CAN-TP”。

## 3. 现有链路与改造后边界

```text
PC Python / Web
        │ UART command
        ▼
F407 Tester Bridge
  ├─ CAN TX FIFO (8)
  └─ CAN RX FIFO (8)
        │ CAN 0x601 / 0x602 / 0x7DF
        ▼
F103 CanIf
  └─ CAN TX FIFO (16)
        ▼
F103 CanTp            ← V2.9 新的状态机中心
  ├─ Rx session: FF -> FC -> CF -> completed PDU
  ├─ Tx session: FF -> wait FC -> CF -> complete
  └─ N_Bs / N_Cr / SN validation
        ▼
UDS Server / DID Manager
```

层间约束不变：`UdsServer` 不直接编码 CAN 数据；`EcuApp` 不自行拼接 FF/CF；CanTp 通过 `CanIf_Transmit()` 发送原始 CAN 帧。

## 4. 帧格式与初始参数

所有帧 DLC 固定为 8，不使用 padding 中的数据。

| 帧类型 | PCI | 本项目 V2.9 解释 |
|---|---|---|
| SF | `0x0L` | 已有路径，`L=1..7` |
| FF | `0x1Lh Ll` | `12 bit` 总 PDU 长度，随后携带前 6 字节 |
| FC-CTS | `0x30 BS STmin` | 允许发送；初始实现使用 `BS=0`、`STmin=0` |
| CF | `0x2SN` | 序号从 1 起循环至 15，每帧至多 7 字节 |

初始配置：

```text
CANTP_MAX_PDU_LEN       = 256 bytes
CANTP_N_BS_TIMEOUT_MS   = 1000 ms   # ECU 发 FF 后等待 FC
CANTP_N_CR_TIMEOUT_MS   = 1000 ms   # ECU 收 FF/CF 后等待下一 CF
CANTP_DEFAULT_BS        = 0          # 不要求中途再次 FC
CANTP_DEFAULT_STMIN     = 0 ms
```

`BS=0` 只代表第一版允许连续发送直到 PDU 完成；后续可以在不改变 UDS 层接口的情况下加入非零 BS 以及 STmin 调度。

## 5. 状态机

### 5.1 ECU 发送多帧响应

```text
UDS response len > 7
  -> encode / enqueue FF
  -> TX_WAIT_FC (启动 N_Bs)
  -> receive FC(CTS)
  -> TX_SEND_CF (SN=1)
  -> enqueue remaining CF frames
  -> TX_IDLE
```

若 N_Bs 超时、收到错误 FC，或 `CanIf` 队列最终拒绝帧，则记录 CanTp 错误并结束本次传输。CanIf 的 FIFO 负责 mailbox 暂满时的重试；CanTp 不把“暂未硬件发送”误判为 PDU 成功。

### 5.2 ECU 接收多帧请求

```text
receive FF
  -> validate total length (8..256)
  -> copy first 6 bytes to RX buffer
  -> enqueue FC(CTS)
  -> RX_WAIT_CF (expected SN=1, 启动 N_Cr)
  -> receive CF with expected SN
  -> append payload / expected SN++
  -> full PDU: deliver to UDS Server
```

CF 序号不匹配、长度越界或 N_Cr 超时均丢弃当前会话，不把半包数据交给 UDS。

### 5.3 单帧兼容规则

```text
SF request  -> 继续走既有 UDS 分发路径
SF response -> 继续使用既有编码路径
```

因此现有 F190、F186、F187、0x10/0x14/0x19/0x22/0x27/0x2E/0x31/0x3E 的 16 步回归是每一个 V2.9 小阶段的硬门槛。

## 6. 代码接口设计

`cantp.h` 将从当前的 Single Frame 包装层升级为传输层公共接口，保留旧接口作为兼容入口：

```c
void CanTp_Init(void);
void CanTp_MainFunction(uint32_t now_ms);
CanTp_StatusType CanTp_RxIndication(uint32_t can_id,
                                    const uint8_t *data,
                                    uint8_t dlc,
                                    uint32_t now_ms);
CanTp_StatusType CanTp_Transmit(uint32_t tx_can_id,
                                const uint8_t *pdu,
                                uint16_t pdu_len,
                                uint32_t now_ms);
bool CanTp_IsRxPduAvailable(void);
CanTp_StatusType CanTp_GetRxPdu(uint8_t *buffer,
                                uint16_t buffer_len,
                                uint16_t *pdu_len);
```

实现上使用静态 `rx_buffer[256]` 和 `tx_buffer[256]`。`EcuApp` 的职责变化为：把所有目标 CAN 帧交给 `CanTp_RxIndication()`，在获得完整 PDU 后调用 `UdsServer_Dispatch()`，最后调用 `CanTp_Transmit()`。它不再假设一个 UDS 请求或响应必然等于一个 CAN 帧。

为支持最长 256 字节 PDU：

- `CanTp_FrameViewType.payload_len` 将从 `uint8_t` 扩展为 `uint16_t`；
- `UDS_SERVER_MAX_RESPONSE_LEN` 将扩展到与 CanTp 缓冲边界一致的受控值；
- DID 回调长度和 UDS 分发长度相关形参将同步审计，避免 `uint8_t` 截断。

## 7. 上位机与 Tester 的职责

F407 当前已经可以可靠地完成“向 CAN 发原始帧”和“从 CAN RX FIFO 读取多个帧”。因此第一版多帧客户端逻辑放在 Python 工具中：

```text
Python uds-multi
  -> send SF / FF using existing F407 CAN_TX bridge
  -> poll or drain CAN frames
  -> on FF response send FC(CTS)
  -> reassemble CF sequence and check SN/timeout
```

这不是绕开 Tester：每一帧仍经过 PC UART -> F407 FreeRTOS Bridge -> F407 CAN TX/RX 队列 -> CAN 总线。这样能先验证 ECU 协议栈，再决定是否把 Tester 侧 CanTp client 下沉为固件模块。

## 8. 分阶段实施与验收

### V2.9-1：传输层核心与静态验证

- 新增 CAN-TP 帧编解码、TX/RX session 状态机及超时单元测试；
- 不改外部 F103 工程，不烧录；
- 明确 SF 兼容用例与 FF/FC/CF 编解码向量。

通过条件：主机侧单元测试覆盖 FF 长度、CF 序号、FC、超时、长度溢出。

### V2.9-2：ECU 多帧响应

- 接入 F103 `EcuApp`；
- 新增只读演示 DID `0xF192`，返回固定 20 字节以上诊断数据；
- Python 增加 `uds-multi` 接收端，收到 ECU FF 后发送 FC(CTS) 并重组 CF；
- 仍保留所有既有 SF 请求与响应。

通过条件：

```text
22 F1 92 -> ECU FF -> Tester FC -> ECU CF... -> host 完整重组
完整固件回归 16/16 PASS
V2.5 顺序压力测试 20/20 PASS
```

### V2.9-3：ECU 多帧请求

- Python 支持发送 FF、等待 ECU FC、发送 CF；
- ECU 仅把完整 PDU 交给 UDS；
- 选择一个受控长写入 DID 或专用 Routine 数据作为验收载荷。

通过条件：正确请求成功；SN 错误、CF 超时、总长度超限都不改变应用数据，并留下可观测错误状态。

## 9. 风险与保护措施

- F103 RAM 有限：默认 256 字节静态缓冲，不允许动态分配；
- 当前 CAN 队列是异步边界：CanTp 会对队列满返回明确错误，不静默丢帧；
- 不能一次迁移所有路径：每一步都必须先通过现有 16/16，再做新多帧用例；
- 功能寻址 `0x7DF` 仍只按现有 SF 用例验收，避免多 ECU 同时响应的冲突问题被误当作单节点 CAN-TP 功能。

## 10. 当前结论

V2.8 已提供多帧传输需要的帧级队列基础，但并不等于 CAN-TP 多帧已经实现。V2.9 将以“可回滚的协议状态机 + 固定缓冲区 + 每阶段真实硬件验收”的方式完成 FF/FC/CF，而不是直接堆叠一个长 DID。

## 11. V2.9-1 当前实现记录

已完成但尚未接入 ECU 收发路径的内容：

```text
firmware/common/inc/cantp.h
firmware/common/src/cantp.c
```

新增线协议 helper：

```text
CanTp_GetPciType()
CanTp_EncodeFirstFrame()
CanTp_EncodeConsecutiveFrame()
CanTp_EncodeFlowControlCts()
```

现有 `CanTp_DecodeSingleFrame()` / `CanTp_EncodeSingleFrame()` 未改变；所以当前 F103 固件行为和 16/16 回归协议不受这一步影响。

主机侧新增纯 Python codec/reassembler：

```text
tools/pc_diag/cantp_codec.py
tests/test_cantp_codec.py
```

本地静态测试结果：

```text
python -m pytest \
  tests\test_cantp_codec.py tests\test_minidcm_uart_link.py -q

11 passed
```

覆盖项：SF 编码、FF 长度编码、FC CTS、FF→FC→CF 重组、CF 序号错误和非法长度。同步脚本也已纳入 `cantp.[ch]` 与 `cantp_sf.[ch]`，后续板端状态机接入时不会再次漏同步传输层文件。

## 12. V2.9-2：ECU 多帧响应实现（待硬件验收）

本次只扩展 ECU **响应方向**，请求方向保持现有 Single Frame：

```text
Tester -> ECU request: 03 22 F1 92 00 00 00 00
ECU -> Tester response: FF -> wait FC(CTS) -> CF1 -> CF2 -> CF3
```

新增只读 DID：

```text
DID 0xF192
data = ASCII "MINIDCM-CANTP-MF-V2.9" (21 bytes)
UDS positive response = 62 F1 92 + 21 bytes = 24 bytes
```

预期 CAN 帧序列：

```text
ECU 0x602 FF : 10 18 62 F1 92 4D 49 4E
Tester 0x601 FC: 30 00 00 00 00 00 00 00
ECU 0x602 CF1: 21 49 44 43 4D 2D 43 41
ECU 0x602 CF2: 22 4E 54 50 2D 4D 46 2D
ECU 0x602 CF3: 23 56 32 2E 39 00 00 00
```

实现边界：

- `CanTp_Transmit()`：响应不超过 7 字节时仍发送 SF；超过 7 字节时复制到固定 TX buffer，发送 FF 并进入 `WAIT_FC`；
- `CanTp_HandleFlowControl()`：只接受 CTS (`0x30`)；V2.9-2 允许 `BS=0 / STmin=0`；
- `CanTp_MainFunction()`：收到 CTS 后将 CF 依次提交给 F103 CanIf TX FIFO；若 FIFO 暂满则下一轮继续；
- N_Bs 为 1000 ms。等不到 FC 时结束本次 TX session；
- `EcuApp` 遇到 FC 不再把它当作 UDS 请求计数，而是交给 CanTp；
- 当前没有实现 FF/CF 请求接收，此项属于 V2.9-3。

仓库静态检查：

```text
python -m py_compile tools/pc_diag/serial_bridge.py tools/pc_diag/cantp_codec.py
python -m pytest tests -q

11 passed
```

F103 CubeIDE 工程已同步：

```text
firmware/targets/F103_ECU_Server
backup:
<workspace>\.codex-stage\f103_v2_stage1_backup_20260712_215232
```

### 硬件验收命令

1. 在 CubeIDE 对 `F103_ECU_Server` 执行 Clean + Build，烧录 F103；F407 不需要重新烧录。
2. 复位 F103 与 F407。
3. 运行：

```powershell
python .\tools\pc_diag\serial_bridge.py uds-multi --port COM17 --baudrate 9600 --hex "22 F1 92" --timeout-ms 1500
```

成功时应包含：

```text
ok: true
flowControlSent: true
udsResponseHex:
62 F1 92 4D 49 4E 49 44 43 4D 2D 43 41 4E 54 50 2D 4D 46 2D 56 32 2E 39
```

4. 再跑 Web Workbench 的完整固件回归，预期仍为 `16/16 PASS`；然后可跑 `uds-stress` 20 次确认单帧基线没有退化。

### V2.9-2 硬件验收完成（2026-07-12）

真实硬件结果：

```text
Command:
serial_bridge.py uds-multi --port COM17 --baudrate 9600 --hex "22 F1 92" --timeout-ms 1500

Result:
  ok: true
  flowControlSent: true
  pollAttempts: 4
  CAN TX acknowledgement: accepted, HAL state 0x02, error 0x00000000
```

实测帧序列与设计一致：

```text
10 18 62 F1 92 4D 49 4E
21 49 44 43 4D 2D 43 41
22 4E 54 50 2D 4D 46 2D
23 56 32 2E 39 00 00 00
```

主机完整重组：

```text
62 F1 92 4D 49 4E 49 44 43 4D 2D 43 41 4E 54 50 2D 4D 46 2D 56 32 2E 39
```

随后既有 Web Workbench 完整固件回归维持：

```text
16/16 PASS
```

结论：V2.9-2 已在真实 PC → F407 → CAN → F103 → CAN → F407 → PC 链路中完成 ECU 多帧响应。当前实现不应表述为“完整 CAN-TP”：多帧请求接收、CF 超时、SN 错误等接收方向状态机仍属于 V2.9-3。

## 13. V2.9-3：ECU 多帧请求实现（待硬件验收）

### F103 接收状态机

新增 RX 会话：

```text
RX_IDLE
  receive FF
  -> validate total length 8..256
  -> copy first 6 PDU bytes
  -> enqueue FC(CTS) on 0x602
  -> RX_WAIT_CF (expected SN=1, N_Cr=1000 ms)

RX_WAIT_CF
  receive expected CF -> append data and increment SN
  complete PDU -> make PDU available to EcuApp
  bad SN / insufficient DLC / N_Cr timeout -> discard partial PDU
```

`EcuApp` 只在 `CanTp_GetRxPdu()` 取得完整 PDU 后调用 UDS Server，因此半包、错序包或超时包不会修改 DID 数据。

### 验收 DID

新增 `0xF193`：16 字节、可读可写、需既有 SecurityAccess 解锁。

```text
write PDU = 2E F1 93 + 16-byte application data
total length = 19 bytes -> FF + 2 CF
```

本次固定演示数据：

```text
ASCII: "MULTIFRAME-RX-OK"
Hex:   4D 55 4C 54 49 46 52 41 4D 45 2D 52 58 2D 4F 4B
```

预期请求帧：

```text
Tester 0x601 FF : 10 13 2E F1 93 4D 55 4C
ECU    0x602 FC : 30 00 00 00 00 00 00 00
Tester 0x601 CF1: 21 54 49 46 52 41 4D 45
Tester 0x601 CF2: 22 2D 52 58 2D 4F 4B 00
ECU    0x602 SF : 03 6E F1 93 00 00 00 00
```

读回 `22 F1 93` 的正响应为 19 字节，因此会复用 V2.9-2 响应路径：ECU FF、Tester FC、ECU 2 个 CF。

### 主机命令

新增：

```text
serial_bridge.py uds-multi-tx
```

行为：发送 FF，等待并校验 ECU FC(CTS)，发送全部 CF，然后接收并解析 SF 或多帧 UDS 响应。它与 `uds-multi` 分工如下：

```text
uds-multi    : SF request -> multi-frame response
uds-multi-tx : multi-frame request -> SF/multi-frame response
```

静态验证：

```text
python -m py_compile tools/pc_diag/serial_bridge.py tools/pc_diag/cantp_codec.py
python -m pytest tests -q

12 passed
```

### 后续硬件验收命令

烧录新 F103 固件并复位两块板后：

```powershell
# 1. 既有 SecurityAccess 解锁
python .\tools\pc_diag\serial_bridge.py uds --port COM17 --baudrate 9600 --hex "27 01"
python .\tools\pc_diag\serial_bridge.py uds --port COM17 --baudrate 9600 --hex "27 02 B7 91 F3 DD"

# 2. FF -> ECU FC -> CF 写入 F193，预期 6E F1 93
python .\tools\pc_diag\serial_bridge.py uds-multi-tx --port COM17 --baudrate 9600 --hex "2E F1 93 4D 55 4C 54 49 46 52 41 4D 45 2D 52 58 2D 4F 4B" --timeout-ms 1500

# 3. SF 读请求 -> ECU FF/CF 响应，预期完整 62 F1 93 + 16 bytes
python .\tools\pc_diag\serial_bridge.py uds-multi --port COM17 --baudrate 9600 --hex "22 F1 93" --timeout-ms 1500
```

最后仍需完整固件回归 `16/16 PASS`。负路径（错误 SN 与 N_Cr 超时）在成功路径稳定后作为 V2.9-4 小阶段加入专用命令和验收。

### 首次 V2.9-3 硬件结果与修复（2026-07-13）

首次实测的传输结果是正确的：

```text
Tester FF -> ECU FC(CTS) -> Tester CF1/CF2
```

ECU 返回 `7F 2E 31`，随后读 `F193` 也能触发正确的多帧响应，但数据仍是全零。这说明：

- CAN-TP RX 状态机已收齐请求；
- FC 与 CF 序列无误；
- 应用层 `WriteDID F193` 没有真正写入。

根因是实现时 `F193` 的写入拷贝分支误放入 `DidMgr_Read()`，而真正的 `DidMgr_Write()` 只处理了 F187。于是配置表虽标记 F193 可写，最终仍返回 `DID_MGR_NOT_FOUND`，映射为 NRC `0x31`。

修复：

```text
从 DidMgr_Read() 删除错误的写入分支；
将 F193 的 16 字节拷贝逻辑加入 DidMgr_Write()；
```

同步后的 F103 工程备份：

```text
<workspace>\.codex-stage\f103_v2_stage1_backup_20260713_102346
```

修复后需要重新 Build/Flash F103，再重复本节的三条验收命令。完整固件回归在发现此应用层问题时仍保持 `16/16 PASS`，说明旧基线未被破坏。

### V2.9-3 正向硬件链路通过（2026-07-13）

修复后的真实硬件结果：

```text
SecurityAccess seed : 67 01 12 34 56 78
SecurityAccess key  : 67 02
```

多帧 WriteDID：

```text
FF request : 10 13 2E F1 93 4D 55 4C
ECU FC     : 30 00 00 00 00 00 00 00
CF1        : 21 54 49 46 52 41 4D 45
CF2        : 22 2D 52 58 2D 4F 4B 00
UDS result : 6E F1 93
```

多帧 ReadDID 回读：

```text
ECU FF : 10 13 62 F1 93 4D 55 4C
Tester FC: 30 00 00 00 00 00 00 00
ECU CF1: 21 54 49 46 52 41 4D 45
ECU CF2: 22 2D 52 58 2D 4F 4B 00

Reassembled UDS response:
62 F1 93 4D 55 4C 54 49 46 52 41 4D 45 2D 52 58 2D 4F 4B
ASCII payload: MULTIFRAME-RX-OK
```

结论：CAN-TP 的 FF/FC/CF 在两个方向都已完成真实硬件正向验证。下一硬门槛是本次 F193 修复后的完整固件回归 `16/16 PASS`；随后再进入错误 SN 与 N_Cr 超时负路径。

### V2.9-3 完整验收完成（2026-07-13）

F193 写入分支修复并重新烧录 F103 后，完整固件回归结果：

```text
16/16 PASS
```

因此 V2.9-3 正向能力正式冻结：

```text
SF request  -> multi-frame ECU response : hardware PASS
multi-frame request -> SF ECU response  : hardware PASS
multi-frame request -> multi-frame read-back response : hardware PASS
existing UDS/DTC/SecurityAccess regression : 16/16 PASS
```

V2.9 下一步仅补负路径与可观测性，不改变上述通过的 wire contract。

## 14. V2.9-4：错误 SN 与 N_Cr 超时（待硬件验收）

### 错误处理策略

CanTp 新增待消费错误状态。`EcuApp` 在每轮 `CanTp_MainFunction()` 后读取该状态并映射到已有 MiniDCM 错误模块：

| CAN-TP 条件 | ECU 行为 | `F18C Last Error` |
|---|---|---|
| CF SN 不等于 expected SN | 丢弃当前 RX session | `02 00` (`MINIDCM_E_CANTP_FORMAT`) |
| FF 后 1000 ms 未收到下一 CF | 丢弃当前 RX session | `00 04` (`MINIDCM_E_TIMEOUT`) |

这些错误也会进入既有 EventLog / DTC 错误链路；本阶段不要求把它们声明为新的永久 DTC 功能。

### 主机注入命令

新增：

```text
serial_bridge.py uds-multi-negative --mode bad-sn
serial_bridge.py uds-multi-negative --mode timeout
```

两个命令都会先发送 FF，并确认 ECU 返回 `FC CTS (30 00 00)`：

- `bad-sn`：故意发送 `SN=2` 的第一帧 CF；ECU 本应期待 `SN=1`；
- `timeout`：收到 ECU FC 后不再发送 CF，等待至少 1000 ms。

建议使用与正确写入值不同的数据，以证明不完整 PDU 不会改写原 F193：

```text
ASCII: SHOULD-NOT-WRITE
Hex:   53 48 4F 55 4C 44 2D 4E 4F 54 2D 57 52 49 54 45
PDU:   2E F1 93 + above 16 bytes
```

### 验收命令

每一种负路径前，先用 `uds-multi --hex "22 F1 93"` 记录 F193 当前值；随后：

```powershell
$bad = "2E F1 93 53 48 4F 55 4C 44 2D 4E 4F 54 2D 57 52 49 54 45"

# 错 SN：应只完成 FF/FC，再注入错误 CF；不应产生 0x2E 正响应
python .\tools\pc_diag\serial_bridge.py uds-multi-negative --port COM17 --baudrate 9600 --mode bad-sn --hex $bad --timeout-ms 1500
python .\tools\pc_diag\serial_bridge.py uds --port COM17 --baudrate 9600 --hex "22 F1 8C"
python .\tools\pc_diag\serial_bridge.py uds-multi --port COM17 --baudrate 9600 --hex "22 F1 93" --timeout-ms 1500

# N_Cr 超时：收到 FC 后不发 CF，等待 1100 ms
python .\tools\pc_diag\serial_bridge.py uds-multi-negative --port COM17 --baudrate 9600 --mode timeout --wait-ms 1100 --hex $bad --timeout-ms 1500
python .\tools\pc_diag\serial_bridge.py uds --port COM17 --baudrate 9600 --hex "22 F1 8C"
python .\tools\pc_diag\serial_bridge.py uds-multi --port COM17 --baudrate 9600 --hex "22 F1 93" --timeout-ms 1500
```

预期：两次 F193 读回均仍是 `MULTIFRAME-RX-OK`；错 SN 后 F18C 为 `62 F1 8C 02 00`；超时后 F18C 为 `62 F1 8C 00 04`。最后重跑完整固件回归，预期 `16/16 PASS`。

### V2.9-4 硬件验收完成（2026-07-13）

错 SN 注入实测：

```text
FF: 10 13 2E F1 93 53 48 4F
ECU FC: 30 00 00 00 00 00 00 00
Injected wrong CF: 22 55 4C 44 2D 4E 4F 54

F18C: 62 F1 8C 02 00
```

超时注入实测：

```text
FF: 10 13 2E F1 93 53 48 4F
ECU FC: 30 00 00 00 00 00 00 00
No CF sent for 1100 ms

F18C: 62 F1 8C 00 04
```

两次异常后读取 F193 均得到 16 个 `0x00`。本次烧录后的 F193 初始值本来就是全零；关键观察是错误请求没有把它改写为测试载荷 `SHOULD-NOT-WRITE`，因此不完整 PDU 未进入 UDS WriteDID 路径。

最终完整固件回归：

```text
16/16 PASS
```

## 15. V2.9 阶段结论

V2.9 已完成并通过真实硬件验证的范围：

```text
CAN-TP Single Frame 兼容
CAN-TP multi-frame response: FF -> FC -> CF
CAN-TP multi-frame request: FF -> FC -> CF -> complete PDU
CF sequence-number rejection
N_Cr timeout rejection (1000 ms)
static transport buffer bound: 256 bytes
F193 protected multi-frame WriteDID and multi-frame read-back
post-change legacy firmware regression: 16/16 PASS
```

仍不应声称为完整 ISO 15765-2 实现：非零 Block Size、STmin 调度、Wait/Overflow Flow Control、发送方向超时的硬件负路径、扩展长度以及多 ECU 并发寻址未完成。它们是后续运输层增强项，而不是本 V2.9 的既成事实。
