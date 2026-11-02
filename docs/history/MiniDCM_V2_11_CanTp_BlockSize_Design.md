> 历史设计记录：原开发目录中的路径、阶段目标与实验记录按当时状态保留。当前构建、运行和验证入口见[仓库 README](../../README.md)。部分原始日志与规划资料未纳入公开快照。

# MiniDCM V2.11：CAN-TP Block Size 分块流控

日期：2026-07-13  
状态：已冻结（双向 Block Size 验收与完整回归均通过）

## 目标

在 V2.9 的 FF / FC / CF 基础上实现非零 Block Size（BS）语义。BS 不再只是被解析后忽略的字段：发送端每发出 BS 个连续帧（CF）后，必须暂停并等待下一帧 FC(CTS) 才能继续。

本阶段刻意不实现 STmin 延时调度、FC WAIT 或 FC OVFLW；它们保留给后续独立阶段，避免把多个时序变化混在一次迁移中。

## 固件设计

### ECU 多帧响应发送方向

收到 FC(CTS) 后，`CanTp_HandleFlowControl()` 保存其中的 `block_size` 并清零当前块内 CF 计数。

`CanTp_MainFunction()` 每成功提交一个 CF 就递增块内计数：

```text
CF submitted
  -> PDU complete: TX idle / complete counter++
  -> BS == 0: keep sending
  -> CF count == BS: TX_WAIT_FC + N_Bs deadline
```

因此 tester 以 `FC: 30 02 00` 响应 ECU 的 FF 时，ECU 对一个需要 3 个 CF 的响应会发送 CF1、CF2，等待下一帧 FC，再发送 CF3。

### ECU 多帧请求接收方向

F103 在收到 FF 时不再回复无限制 `30 00 00`，而是声明 `30 02 00`。每接收 2 个 CF，若 PDU 尚未重组完成，就再次发送 `30 02 00`。这使 tester 的多帧请求发送端也必须实现“分块发送—等待下一 FC”。

## 上位机配合

`uds-multi` 新增：

```powershell
--fc-block-size 2
```

该选项使 PC 在重组 ECU 多帧响应时发送 `30 02 00`，并在每个块完成后自动再次发送 FC。默认仍是 `0`，保持 V2.10 的无限制 CTS 行为。

`uds-multi-tx` 会读取 ECU 的 FC Block Size；若为非零，则在每个块末尾暂停并等待 ECU 的下一帧 FC。

## 验收

### 发送方向：ECU 按 tester 的 BS=2 发送

```powershell
python .\tools\pc_diag\serial_bridge.py uds-multi --port COM17 --baudrate 9600 --hex "22 F1 92" --fc-block-size 2 --timeout-ms 1500
```

预期：完整 `F192` 内容返回；`flowControlCount` 为 2（FF 后一次、CF2 后一次）；CAN 响应帧为 FF + 3 CF。

### 接收方向：ECU 声明 BS=2

现有 `F193` 写入请求只有 2 个 CF，足以验证初始 `30 02 00`，但不会跨越第二个 FC 边界。为此新增 `F1A6`：24-byte、受 SecurityAccess 保护、可读写的验收暂存 DID。其 `0x2E` 请求长度为 27 byte（FF + 3 CF），恰好跨越 BS=2 边界；不改变已有 F193 的 16-byte 兼容契约。

### 回归门槛

完成上述专项验收后，必须再跑 Web Workbench 的完整固件回归，预期 `16/16 PASS`。

## 实机验收记录

2026-07-13，在 F103 同步、编译、烧录并复位后执行：

```powershell
python .\tools\pc_diag\serial_bridge.py uds-multi --port COM17 --baudrate 9600 --hex "22 F1 92" --fc-block-size 2 --timeout-ms 1500
```

结果：

```text
ECU frames: FF + CF1 + CF2 + CF3
requestedFlowControlBlockSize: 2
flowControlCount: 2
UDS payload: 62 F1 92 "MINIDCM-CANTP-MF-V2.9"
```

`flowControlCount=2` 是本阶段的关键证据：第一帧 FC 用于允许 FF 后的第一块，第二帧 FC 在 CF2 后才允许最后一帧 CF3 发送。因此这不是仅有字段值的兼容实现，而是已经实际发生暂停—恢复的 BS 状态机。

随后执行 Web Workbench 完整固件回归：

```text
16/16 PASS
```

原有物理/功能寻址、会话、安全访问、DTC 和错误 NRC 路径均未回归。

### 接收方向重复 FC 验收

为跨越 F103 接收端的 BS 边界，解锁后执行 27-byte `WriteDataByIdentifier(F1A6)`：

```text
UDS request: 2E F1 A6 "BS2-RX-REPEATED-FC-OK!!!"
CAN-TP request: FF + CF1 + CF2 + CF3
ECU FC frames: 30 02 00, 30 02 00
UDS response: 6E F1 A6
```

随后以 `0x22 F1 A6` 读取，并让 tester 对 ECU 响应也使用 `BS=2`：

```text
ECU frames: FF + CF1 + CF2 + CF3
tester flowControlCount: 2
readback: 62 F1 A6 "BS2-RX-REPEATED-FC-OK!!!"
```

这同时证明了：

1. ECU RX 状态机在收到 FF 后使用 BS=2，CF2 后再次发送 FC；
2. PC 多帧请求发送器正确等待重复 FC；
3. 只有接收完整的 CF3 后，UDS WriteDID 才被执行；
4. ECU TX 状态机也能够在同一条读回路径按 BS=2 暂停并恢复。

## 回归工具抗残帧改进

在一次超时后，旧 CAN 响应可能晚于原请求到达 F407 RX 队列；如果测试工具只按 CAN ID 接收下一帧，后续步骤会发生响应错位。`serial_bridge.py uds` 因此增加 UDS SID 关联过滤：仅接受 `request SID + 0x40` 的正响应，或 `7F + request SID` 的负响应；其它 `0x602` 单帧会被从 Tester 队列消费并记录到 `ignoredResponseFrames`，工具继续等待当前请求的响应。

例如对 `3E 00`，滞留的 `62 F1 90 ...` 会被忽略，只有 `7E 00` 才能完成当前请求。该机制不替代 CAN ID 过滤，而是在同一 ECU 响应 ID 内补充 UDS 层关联。

## 最终冻结验收

2026-07-13，在新增 F1A6、双向 BS=2 验收以及工具残帧过滤修复后，Web Workbench 执行：

```text
完整固件回归：16/16 PASS
```

覆盖 Tester 信息、物理/功能寻址 F190、功能寻址 TesterPresent、扩展会话、当前会话 DID、DID 长度/不支持 NRC、DTC 触发/查询/清除以及不支持服务等既有路径。V2.11 因此冻结为可回归基线。

## 真实调试记录：超时后的响应错位

一次回归启动阶段发生 F190 超时后，其迟到响应仍保留在 F407 CAN RX 队列。由于早期 CLI 仅按 `0x602` CAN ID 接收，下一步会取走上一请求的响应；连续运行时表现为 F190 响应被误报为 TesterPresent 响应。通过 `can-drain` 观察到残留 `7E 00` 后，确认问题属于上位机响应关联与队列时序，而不是 ECU 服务分发。

修复位置：`tools/pc_diag/serial_bridge.py` 的 `uds` 路径。修复策略：保留 CAN ID 过滤，并增加 UDS SID 过滤，非当前请求的帧进入 `ignoredResponseFrames` 后继续轮询。最终完整回归恢复 `16/16 PASS`。

## 模块演进记录

- 初始位置：V2.9 的 `cantp.c` 仅在 FF 后等待一次 FC，FC 的 BS/STmin 字段被容忍但未参与状态机。
- 拆分原因：多帧协议看似实现，但对非零 BS 没有真正的背压边界，不能证明流控语义。
- 新位置：`CanTp_TxSessionType` 保存 `block_size/cf_in_block`；`CanTp_RxSessionType` 保存接收块计数；PC `CanTpReassembler` 对应维护块计数。
- 验证方式：PC codec 单元测试覆盖重复 FC；实机通过 `uds-multi --fc-block-size 2` 验证 ECU 发送暂停/恢复；后续用长请求验证 ECU 接收端重复 FC。
