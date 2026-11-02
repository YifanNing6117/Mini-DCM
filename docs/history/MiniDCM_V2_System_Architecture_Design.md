> 历史设计记录：原开发目录中的路径、阶段目标与实验记录按当时状态保留。当前构建、运行和验证入口见[仓库 README](../../README.md)。部分原始日志与规划资料未纳入公开快照。

# MiniDCM V2.0 系统架构设计方案：轻量级嵌入式诊断框架

> 文档定位：这是 MiniDCM V2.0 的系统架构设计方案草案。它基于 `MiniDCM_V1_Baseline_Freeze.md`，目标是指导后续工程化重构，而不是立刻新增协议功能。  
> 当前版本：Draft 0.1  
> 更新时间：2026-07-06  
> 设计原则：不推翻 V1.0 已验证闭环；在保持回归通过的前提下，逐步演进为更工程化的轻量级诊断框架。

---

## 1. V2.0 架构设计目标

V2.0 的目标不是“多支持几个 UDS 服务”，而是让 MiniDCM 从 V1.0 的可运行 MVP 演进为一个更像真实嵌入式工程的轻量级诊断框架。

目标包括：

- 保留 V1.0 已验证硬件闭环；
- 保留 F407 Tester + F103 ECU Server 的双节点结构；
- 保留 Python/Web Workbench 作为主验证入口；
- 明确模块边界，降低 `main.c` 和应用层复杂度；
- 引入统一基础设施：类型、错误码、调度、定时器、缓冲区、日志；
- 让 UDS 服务、DID、Session、Security 逐步配置化；
- 为 CAN-TP 多帧、RoutineControl、DTC、ECUReset 等后续功能预留干净扩展点；
- 为未来迁移 RTOS 留出任务边界，但 V2.0 初期仍保持裸机超级循环。

一句话目标：

> MiniDCM V2.0 = 以 UDS over CAN 为案例的轻量级嵌入式诊断框架。

---

## 2. V2.0 总体系统链路

V2.0 不改变 V1.0 的外部硬件链路。

```text
Browser / Python CLI
  -> Node Backend / serial_bridge.py
  -> CH340 UART
  -> STM32F407 Tester Node
  -> CAN1
  -> SN65HVD230
  -> CANH / CANL / GND
  -> SN65HVD230
  -> STM32F103 ECU Server
  -> MiniDCM Framework
  -> CAN Response
  -> STM32F407 Tester Node
  -> UART JSON Result
  -> Browser / Python CLI
```

V2.0 改变的是 F103 ECU Server 内部结构，以及部分 F407 Tester 和上位机工具的组织方式。

---

## 3. V2.0 分层架构

建议 V2.0 固件分层如下：

```text
Application Layer
  - Did Data Provider
  - Routine Application
  - Sensor Application
  - DTC Event Source

Diagnostic Layer / DCM
  - UdsServer
  - Service Dispatcher
  - NRC Builder
  - SessionMgr
  - SecurityMgr
  - DidMgr
  - RoutineMgr
  - DtcMgr

Transport Layer
  - CanTp
  - CanTp Single Frame
  - Future: FF / FC / CF Multi Frame
  - Transport Timer Handling

CAN Interface Layer
  - CanIf
  - RX Queue
  - TX Wrapper
  - Filter Config
  - CAN Error Counter

Framework Infrastructure
  - Scheduler
  - TimerMgr
  - BufferMgr
  - Log / Trace
  - ErrorCode
  - Std_Types

MCAL / BSP Layer
  - STM32 HAL / LL
  - CAN Driver Binding
  - UART Driver Binding
  - GPIO / TIM / I2C Binding
```

这里的重点是：

- Diagnostic Layer 不直接调用 HAL；
- Application Layer 不直接处理 CAN 帧；
- CanIf 不理解 UDS；
- CanTp 不理解 DID；
- UdsServer 不直接操作 GPIO 或具体传感器；
- Timer、Buffer、Log 不属于某个业务模块，而是框架基础设施。

---

## 4. V2.0 目录架构设计

V2.0 建议保留 CubeIDE 工程目录，同时建立更明确的公共框架目录。

目标目录：

```text
firmware/
  common/
    inc/
      minidcm_types.h
      minidcm_error.h
      minidcm_version.h
      minidcm_config.h

      scheduler.h
      timer_mgr.h
      buffer_mgr.h
      log.h

      can_if.h
      can_tp.h
      cantp_sf.h

      uds_server.h
      uds_cfg.h
      nrc.h
      did_mgr.h
      did_cfg.h
      session_mgr.h
      security_mgr.h
      routine_mgr.h
      dtc_mgr.h

      minidcm_uart_link.h

    src/
      scheduler.c
      timer_mgr.c
      buffer_mgr.c
      log.c

      can_if.c
      can_tp.c
      cantp_sf.c

      uds_server.c
      uds_cfg.c
      nrc.c
      did_mgr.c
      did_cfg.c
      session_mgr.c
      security_mgr.c
      routine_mgr.c
      dtc_mgr.c

      minidcm_uart_link.c

  ecu_server/
    F103_ECU_Server.ioc
    Core/
    Drivers/
    App/
      ecu_app.h
      ecu_app.c
      ecu_bsp.h
      ecu_bsp.c
      ecu_config.h
      ecu_config.c

  tester/
    F407_Tester.ioc
    Core/
    Drivers/
    App/
      tester_app.h
      tester_app.c
      uart_cmd.h
      uart_cmd.c
      tester_bsp.h
      tester_bsp.c
      tester_config.h
      tester_config.c
```

### 4.1 为什么不直接把 CubeIDE 目录大改

CubeMX/CubeIDE 会生成 `Core/`、`Drivers/`、`.ioc`、linker script、startup 等文件。直接大规模移动这些文件风险很高。

所以 V2.0 的原则是：

```text
CubeMX 生成代码尽量保持原位。
手写框架代码放 firmware/common。
板级适配代码放各自 App 目录。
```

---

## 5. V2.0 模块设计

### 5.1 Std Types / Error

新增模块：

```text
minidcm_types.h
minidcm_error.h
minidcm_version.h
```

职责：

- 统一返回值；
- 统一布尔类型；
- 统一错误码；
- 统一版本信息；
- 避免每个模块自己定义 `OK` / `ERROR` / `TRUE` / `FALSE`。

建议接口：

```c
typedef uint8_t Std_ReturnType;

#define E_OK      0u
#define E_NOT_OK 1u

typedef enum {
    FALSE = 0u,
    TRUE  = 1u
} boolean;
```

建议错误码：

```c
typedef enum {
    MINIDCM_ERR_NONE = 0,
    MINIDCM_ERR_PARAM,
    MINIDCM_ERR_BUFFER_FULL,
    MINIDCM_ERR_BUFFER_EMPTY,
    MINIDCM_ERR_TIMEOUT,
    MINIDCM_ERR_CAN_TX_FAILED,
    MINIDCM_ERR_CAN_BUS_OFF,
    MINIDCM_ERR_UNSUPPORTED,
    MINIDCM_ERR_SECURITY_DENIED,
    MINIDCM_ERR_INVALID_STATE
} MiniDcm_ErrorCodeType;
```

### 5.2 Scheduler

新增模块：

```text
scheduler.h
scheduler.c
```

职责：

- 管理裸机周期任务；
- 让 `main.c` 不再手写一堆周期判断；
- 为未来 RTOS task 化预留边界。

建议接口：

```c
typedef void (*Scheduler_TaskFuncType)(void);

void Scheduler_Init(void);
void Scheduler_Run(uint32_t now_ms);
```

内部任务表：

```c
static const Scheduler_TaskConfigType g_scheduler_tasks[] = {
    { CanIf_MainFunction,   1u },
    { CanTp_MainFunction,   1u },
    { Dcm_MainFunction,     10u },
    { EcuApp_MainFunction,  10u },
    { Log_MainFunction,     100u },
};
```

注意：V1.0 里 `EcuApp_MainFunction(now_ms)` 已经存在。V2.0 初期可以先让 Scheduler 调 `EcuApp_MainFunction`，再逐步拆出更多 MainFunction。

### 5.3 TimerMgr

新增模块：

```text
timer_mgr.h
timer_mgr.c
```

职责：

- 管理软件超时；
- 替代各模块内部散落的 tick 判断；
- 服务于 S3 timeout、Security lockout、CAN-TP timeout、Routine timeout。

建议接口：

```c
typedef struct {
    uint32_t start_ms;
    uint32_t timeout_ms;
    boolean active;
} TimerMgr_TimerType;

void TimerMgr_Start(TimerMgr_TimerType *timer, uint32_t now_ms, uint32_t timeout_ms);
void TimerMgr_Stop(TimerMgr_TimerType *timer);
boolean TimerMgr_IsExpired(const TimerMgr_TimerType *timer, uint32_t now_ms);
boolean TimerMgr_IsActive(const TimerMgr_TimerType *timer);
```

迁移顺序：

1. SecurityMgr lockout timer；
2. SessionMgr S3 timer；
3. 后续 CanTp timeout；
4. 后续 Routine timeout。

### 5.4 BufferMgr

新增模块：

```text
buffer_mgr.h
buffer_mgr.c
```

职责：

- 提供通用环形队列；
- 支持 UART RX/TX、CAN RX/TX、日志队列、后续 CAN-TP 多帧缓存；
- 避免每个模块重复实现 head/tail/count。

建议先做轻量版：

```c
typedef struct {
    uint8_t *buffer;
    uint16_t size;
    uint16_t head;
    uint16_t tail;
    uint16_t count;
} BufferMgr_RingBufferType;
```

后续 CanIf 的 RX queue 可以迁移到 BufferMgr，也可以先保持原样，等 BufferMgr 稳定后再迁移。

### 5.5 Log / Trace

新增模块：

```text
log.h
log.c
```

职责：

- 记录关键运行事件；
- 辅助排查 timeout、mailbox full、NRC、buffer overflow；
- 后续通过 UART 或 Web Workbench 暴露统计信息。

第一版建议只做计数和宏开关，不做复杂字符串日志。

可记录事件：

```text
CAN_RX
CAN_TX
CAN_TX_FAIL
CAN_RX_OVERFLOW
UDS_REQUEST
UDS_POS_RESPONSE
UDS_NEG_RESPONSE
SECURITY_UNLOCK
SECURITY_LOCKOUT
SESSION_CHANGE
S3_TIMEOUT
CANTP_ERROR
```

### 5.6 CanIf

V1.0 已有：

- `CanIf_Init`
- `CanIf_Start`
- `CanIf_ReadRxFifo0`
- `CanIf_RxIndication`
- `CanIf_Receive`
- `CanIf_Transmit`
- 统计计数

V2.0 目标：

- 保持现有行为；
- 使用统一类型和错误码；
- 增加 `CanIf_MainFunction`，为 TX 队列和错误恢复预留位置；
- 将 CAN ID 配置逐步从宏迁移到配置结构；
- 继续保持 CanIf 不理解 UDS。

建议未来接口：

```c
Std_ReturnType CanIf_Init(const CanIf_ConfigType *config);
Std_ReturnType CanIf_Start(void);
Std_ReturnType CanIf_Transmit(const CanIf_PduType *pdu);
Std_ReturnType CanIf_RxIndication(const CanIf_FrameType *frame);
Std_ReturnType CanIf_Receive(CanIf_FrameType *frame);
void CanIf_MainFunction(void);
```

### 5.7 CanTp

V1.0 当前状态：

- `CanTp` 是 Single Frame codec 的正式包装层；
- `CanTpSf` 负责实际 Single Frame 编解码；
- 当前只支持 1~7 字节 UDS payload。

V2.0 目标：

- 保持 Single Frame 行为；
- 将接口从 `DecodeSingleFrame/EncodeSingleFrame` 逐步抽象为完整 CanTp API；
- 为未来 FF/FC/CF 多帧状态机预留数据结构；
- 引入 CanTp_MainFunction 和 TimerMgr。

建议未来接口：

```c
Std_ReturnType CanTp_Init(const CanTp_ConfigType *config);
Std_ReturnType CanTp_Transmit(uint32_t target_id, const uint8_t *payload, uint16_t length);
void CanTp_RxIndication(const CanIf_FrameType *frame);
Std_ReturnType CanTp_Receive(uint8_t *payload, uint16_t *length, uint16_t max_len);
void CanTp_MainFunction(void);
```

V2.0 初期可以继续内部只支持 Single Frame，但外部 API 先向完整 CanTp 靠拢。

### 5.8 DCM / UdsServer

V1.0 当前状态：

- `UdsServer` 已经有 SID service table；
- 但服务配置还比较简单，只包含 SID 和 handler；
- NRC 定义仍在 `uds_server.c` 内；
- Session 和 Security 权限判断部分仍写在具体 handler 中。

V2.0 目标：

- 拆出 `nrc.h/.c`；
- 拆出 `uds_cfg.h/.c`；
- 服务表增加 session/security 权限字段；
- handler 更专注于服务本身；
- 统一负响应构造；
- 引入 `Dcm_MainFunction` 或保留 `UdsServer_Dispatch` + 上层调度。

建议服务配置：

```c
typedef struct {
    uint8_t sid;
    Uds_ServiceHandlerType handler;
    uint8_t allowed_sessions;
    boolean security_required;
} Uds_ServiceConfigType;
```

### 5.9 DidMgr

V1.0 当前状态：

- 支持 `DidMgr_Read`；
- 支持 `DidMgr_Write`；
- 内部支持 F190/F186/F187；
- 权限逻辑还没有完全配置化。

V2.0 目标：

- 拆出 `did_cfg.c`；
- DID 用配置表描述；
- DID 配置包括 read/write 函数、长度、session 权限、security 权限；
- `UdsServer` 不直接关心某个 DID 的具体数据来源。

建议 DID 配置：

```c
typedef struct {
    uint16_t did;
    Did_ReadFuncType read;
    Did_WriteFuncType write;
    uint16_t max_len;
    uint8_t allowed_sessions;
    boolean security_required;
} Did_ConfigType;
```

### 5.10 SessionMgr

V1.0 当前状态：

- 支持 Default / Programming / Extended；
- 支持 S3 timeout 5000ms；
- 支持 DID F186 观察当前 session。

V2.0 目标：

- 使用 TimerMgr 管理 S3 timeout；
- 提供统一 session 权限判断函数；
- 服务配置表通过 SessionMgr 判断当前 session 是否允许。

建议接口：

```c
void SessionMgr_Init(void);
Std_ReturnType SessionMgr_SetSession(uint8_t session, uint32_t now_ms);
uint8_t SessionMgr_GetSession(void);
boolean SessionMgr_IsAllowed(uint8_t allowed_mask);
void SessionMgr_OnUdsActivity(uint32_t now_ms);
void SessionMgr_MainFunction(uint32_t now_ms);
```

### 5.11 SecurityMgr

V1.0 当前状态：

- 支持固定 seed；
- key = seed XOR 0xA5；
- 支持失败次数；
- 支持 10 秒 lockout；
- 支持 `SecurityMgr_IsUnlocked`。

V2.0 目标：

- 使用 TimerMgr 管理 lockout；
- 把安全等级抽象出来，不只一个 unlocked 状态；
- 为后续多个 security level 预留；
- 保留当前简单算法作为 demo algorithm。

建议状态：

```text
LOCKED
SEED_REQUESTED
UNLOCKED_LEVEL_1
LOCKOUT
```

### 5.12 RoutineMgr / DtcMgr

V1.0 未实现。

V2.0 架构中预留：

```text
routine_mgr.h/.c
routine_cfg.h/.c
dtc_mgr.h/.c
dtc_cfg.h/.c
```

但不在 Stage 1 立刻实现。

RoutineMgr 后续用于 `0x31 RoutineControl`。  
DtcMgr 后续用于 `0x19` 和 `0x14`。

---

## 6. V2.0 运行模型

V2.0 初期仍然是裸机超级循环，不上 RTOS。

推荐运行模型：

```text
TIM / SysTick 提供毫秒 tick
main.c 初始化 HAL 和 App
while(1)
  -> Scheduler_Run(now_ms)
```

Scheduler 内部按周期调度：

| 任务 | 周期 | 说明 |
|---|---|---|
| `CanIf_MainFunction` | 1ms | 处理 CAN 队列、错误恢复预留 |
| `CanTp_MainFunction` | 1ms | 传输层状态机和 timeout 预留 |
| `Dcm_MainFunction` / `UdsServer` | 10ms | 诊断层周期处理预留 |
| `EcuApp_MainFunction` | 10ms 或 1ms | ECU 应用编排 |
| `SessionMgr_MainFunction` | 10ms | S3 timeout，也可由 EcuApp 调用 |
| `Log_MainFunction` | 100ms | 日志刷新或统计 |
| `Led_MainFunction` | 100ms | 心跳或状态指示 |

第一步不一定要一次性拆出所有 MainFunction，可以先把 V1.0 的 `EcuApp_MainFunction(now_ms)` 接入 Scheduler。

---

## 7. V2.0 数据流设计

### 7.1 物理寻址请求 `0x601 22 F1 90`

目标数据流：

```text
CAN RX interrupt
  -> CanIf_ReadRxFifo0
  -> CanIf_RxIndication
  -> CanIf RX queue

Scheduler
  -> EcuApp_MainFunction
  -> CanIf_Receive
  -> CanTp_RxIndication / Decode
  -> UdsServer_Dispatch
  -> DidMgr_Read(F190)
  -> Uds positive response
  -> CanTp_Transmit / Encode
  -> CanIf_Transmit(0x602)
```

### 7.2 功能寻址请求 `0x7DF 3E 00`

数据流基本相同，只是 request CAN ID 为 `0x7DF`，响应仍然通过 `0x602` 返回。

### 7.3 受保护写 DID `2E F1 87 5A`

```text
UdsServer 识别 SID 0x2E
  -> 检查 request length
  -> 检查 SecurityMgr_IsUnlocked
  -> DidMgr_Write(F187)
  -> 返回 6E F1 87 或 7F 2E NRC
```

V2.0 中应把 “是否需要解锁” 逐步从 handler 内部迁移到 DID 配置表或 UDS 服务配置表。

---

## 8. V2.0 配置化设计

V2.0 的关键能力之一是配置化。

### 8.1 CAN 配置

当前 CAN ID 是宏：

```c
#define CANIF_ECU_REQUEST_ID 0x601u
#define CANIF_FUNCTIONAL_REQUEST_ID 0x7DFu
#define CANIF_ECU_RESPONSE_ID 0x602u
```

V2.0 可以逐步迁移为：

```c
typedef struct {
    uint32_t physical_request_id;
    uint32_t functional_request_id;
    uint32_t physical_response_id;
} CanIf_ConfigType;
```

### 8.2 UDS 服务配置

将 SID、handler、session、安全权限放在配置表中。

### 8.3 DID 配置

将 DID 编号、读函数、写函数、长度、session、安全权限放在配置表中。

### 8.4 测试配置

将 Python/Web 回归测试用例配置化。

最终希望形成：

```text
固件配置表决定 ECU 支持什么。
测试配置表决定上位机验证什么。
文档说明配置表如何扩展。
```

---

## 9. V2.0 和 V1.0 的兼容策略

V2.0 重构必须遵守：

```text
任何阶段完成后，V1.0 回归必须仍然通过。
```

最小回归：

```text
info
ping
0x601 22 F1 90
0x7DF 22 F1 90
0x7DF 3E 00
```

完整回归：

```text
full firmware regression 9/9
SecurityAccess + WriteDID 5/5
```

如果重构过程中回归失败，应优先恢复 V1.0 行为，而不是继续加新功能。

---

## 10. V2.0 第一阶段落地建议

Stage 1 不直接改协议功能。

建议第一轮改动顺序：

1. 创建/确认 `MiniDCM_V1_Baseline_Freeze.md`；
2. 创建本 V2.0 系统架构设计文档；
3. 增加 `minidcm_types.h`，但先少量使用；
4. 增加 `scheduler.h/.c`；
5. 让 F103 `main.c` 通过 Scheduler 调用 `EcuApp_MainFunction`；
6. 不改变 UDS 响应；
7. 跑 V1.0 回归。

第一轮验收：

```text
编译通过
F103/F407 烧录后 reset
info PASS
22 F1 90 PASS
3E 00 PASS
完整固件回归 PASS
SecurityAccess + WriteDID PASS
```

---

## 11. 面试叙事升级

V1.0 可以这样讲：

> 我完成了一个真实硬件闭环的 UDS over CAN MVP，用 F407 做 Tester，F103 做 ECU，通过 CAN-TP Single Frame 和 UDS 服务完成诊断请求响应，并用 Python/Web 做回归验证。

V2.0 可以这样讲：

> 在 V1.0 跑通硬件闭环后，我没有继续盲目堆服务，而是把项目向工程化框架演进。V2.0 重点是统一类型和错误码、引入裸机 Scheduler、Timer Manager、Buffer Manager、Log，并把 UDS 服务和 DID 管理配置化。这样后续新增 RoutineControl、DTC 或 CAN-TP 多帧时，不需要推翻原有结构。

这就是项目从 Demo 到工程的升级点。

---

## 12. 当前设计结论

MiniDCM V2.0 的系统架构应该基于 V1.0 真实链路，不推翻已有成果。

V2.0 第一阶段不应优先做新 SID，而应先做：

```text
Baseline Freeze
  -> Architecture Design
  -> Std Types
  -> Scheduler
  -> TimerMgr
  -> Configurable DCM
```

协议扩展应后置，并作为架构验证案例。

最终目标不是“支持更多协议”，而是：

```text
形成一个小而完整、能讲清楚、能测试、能继续扩展的嵌入式诊断框架。
```
