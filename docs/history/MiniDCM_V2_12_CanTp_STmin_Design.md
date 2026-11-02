> 历史设计记录：原开发目录中的路径、阶段目标与实验记录按当时状态保留。当前构建、运行和验证入口见[仓库 README](../../README.md)。部分原始日志与规划资料未纳入公开快照。

# MiniDCM V2.12：CAN-TP STmin CF 发送间隔

日期：2026-07-13  
状态：已冻结（双向 STmin 验收与完整回归通过）

## 目标

在 V2.11 的 Block Size 暂停/恢复边界之上，实现 ISO-TP Flow Control 中的 STmin（Separation Time Minimum）语义：发送端收到 FC(CTS) 后，连续帧之间不得早于接收端声明的最小间隔。

## 支持范围

F103 的 CAN-TP 主函数由 1ms FreeRTOS/DCM 调度节拍驱动，因此本阶段可靠支持 FC 第三字节的：

```text
0x00 .. 0x7F  -> 0 .. 127 ms
```

ISO-TP 中 `0xF1..0xF9` 的 100–900us 编码需要高分辨率定时器和更细的发送调度，不在当前 1ms 软件调度能力内。F103 收到此范围或保留编码时会拒绝该 FC 并记录 CAN-TP 格式错误，不会把它静默当成 0ms。

PC 工具可解析完整 ISO-TP STmin 编码；但真实硬件验收使用 10ms，避免把 Windows 用户态 sleep 的精度写成硬实时保证。

## F103 状态机变化

`CanTp_TxSessionType` 新增：

```text
st_min_ms  : 已接受的毫秒间隔
next_cf_ms : 下一 CF 允许发送的 ECU tick 时间
```

收到 FC(CTS) 后：

```text
decode STmin
  -> valid: state = SEND_CF, next_cf_ms = now
  -> unsupported: state = IDLE, report CAN-TP format error
```

每成功提交一个 CF 后：

```text
next_cf_ms = now + st_min_ms
```

非零 STmin 时，本次 `CanTp_MainFunction()` 立即返回，下一 CF 必须等待后续 1ms 调度到期；STmin=0 保持 V2.11 的连续发送行为。

## 接收方向与 PC 工具

F103 接收多帧请求时宣告：

```text
FC(CTS) = 30 02 0A
          BS=2, STmin=10ms
```

`uds-multi-tx` 读取每一帧 ECU FC，并在相邻 CF 之间应用对应 STmin；输出 `appliedStMinMsPerCf` 作为主机侧可追溯记录。

`uds-multi` 新增：

```powershell
--fc-stmin 10
```

工具以 `FC=30 <BS> 0A` 回应 ECU FF，并输出 `canResponseFrameIntervalsMs`，用于观察 ECU 响应 CF 的实际到达间隔。

## 验收命令

### ECU TX 遵守 tester STmin

```powershell
python .\tools\pc_diag\serial_bridge.py uds-multi --port COM17 --baudrate 9600 --hex "22 F1 A6" --fc-block-size 2 --fc-stmin 10 --timeout-ms 2000
```

预期：完整 F1A6 多帧读回、`flowControlCount=2`；`canResponseFrameIntervalsMs` 中 CF 间隔应反映约 10ms 或更长（UART/轮询会带来额外延迟）。

### PC TX 遵守 ECU STmin

先完成 SecurityAccess 解锁，然后执行：

```powershell
python .\tools\pc_diag\serial_bridge.py uds-multi-tx --port COM17 --baudrate 9600 --hex "2E F1 A6 53 54 4D 49 4E 2D 31 30 4D 53 2D 52 45 50 45 41 54 2D 46 43 21 21 21 21" --timeout-ms 2000
```

预期：`ecuFlowControls` 中两帧都是 `30 02 0A`，`appliedStMinMsPerCf` 显示 10ms，并返回 `6E F1 A6`。然后以 `0x22 F1 A6` 多帧读回确认写入。

## 过程记录

- 初始位置：V2.11 中 FC 的 BS 已进入状态机，但 STmin 仅作为未使用字段。
- 拆分原因：无节流的 CF 发送无法表征接收端缓冲/任务调度能力，也无法完整解释 FC 的两个控制维度。
- 新位置：`cantp.c` 的 TX session 保存 STmin 与 next-CF 时间；PC 工具在多帧 TX 路径按 ECU FC 插入间隔。
- 验证方式：PC codec 单元测试检查 STmin 编码；真实硬件分别以 tester 指定 STmin 和 ECU 指定 STmin 验证双向路径，最后跑 16/16 回归。

## 实机验收记录

2026-07-13，F103 编译、烧录、复位后完成以下真实链路测试。

### ECU TX 遵守 tester STmin

Tester 对 F192 多帧响应发送 `FC=30 02 0A`，结果：

```text
FF + CF1 + CF2 + CF3 complete
flowControlCount: 2
CAN response frame intervals: 109ms, 62ms, 110ms
UDS response: 62 F1 92 "MINIDCM-CANTP-MF-V2.9"
```

所有可观察到的相邻帧间隔都不小于 10ms。这里的 62–110ms 是 PC UART 命令、F407 bridge 任务和轮询取帧共同形成的端到端观察时间，不应表述为 CAN 控制器上的精确 10ms 波形；它证明的是 ECU 未早于 STmin 继续提交后续 CF。

### PC TX 遵守 ECU STmin

解锁后，以 27-byte UDS 多帧请求写入 F1A6。ECU 两次返回：

```text
30 02 0A
30 02 0A
```

PC 工具记录：

```text
appliedStMinMsPerCf: [10.0, 10.0, 10.0]
UDS response: 6E F1 A6
```

随后按 tester 的 `BS=2, STmin=10ms` 读回：

```text
62 F1 A6 "STMIN-10MS-REPEAT-FC!!!!"
flowControlCount: 2
```

### 最终回归

```text
Web Workbench 完整固件回归：16/16 PASS
```

V2.12 冻结为可回归基线。现有 UDS/DTC/安全访问功能与双向 FF/FC/CF、BS、STmin 能力可同时在真实硬件上运行。
