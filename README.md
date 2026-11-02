# Mini-DCM

基于 STM32F103 ECU 与 STM32F407 Tester 的 UDS over CAN 诊断教学项目，包含 CAN 接口、CAN 传输层、UDS 服务、会话与安全访问、DID、DTC 和 MPU6050 数据接入。适合学习诊断协议分层、FreeRTOS 任务和队列，以及双节点联调。

本发布快照包含完整 CubeIDE 工程和独立协议模块。`firmware/targets` 是当前可构建的 FreeRTOS 工程；`firmware/common` 保留通用模块，供阅读和移植。早期裸机设计的演进记录位于 `docs/history`，当前行为以目标工程源码为准。

## 技术栈与项目重点

本项目的重点是**自行实现 UDS 诊断应用层协议及配套 CAN-TP 传输逻辑**。STM32 HAL 提供外设访问，FreeRTOS 提供任务调度与同步；诊断请求如何分发、数据如何读写、会话如何保持、错误如何应答，由项目的 C 模块处理。UDS（Unified Diagnostic Services，统一诊断服务）定义诊断业务，CAN-TP 负责把一条诊断报文拆成 CAN 帧或从 CAN 帧还原。

| 技术 | 在本项目中的用途 |
|---|---|
| C、STM32 HAL / CMSIS | MCU 初始化、CAN / UART / I2C 访问，协议与板级代码实现 |
| FreeRTOS、CMSIS-RTOS v1 接口 | ECU 与 Tester 任务划分、队列、任务通知、临界区和栈余量观测 |
| 经典 CAN、bxCAN | 两节点间的 500 kbit/s、11-bit ID 通信，收发队列与邮箱管理 |
| CAN-TP | 单帧、多帧分包重组、序号校验、流控与超时状态机 |
| UDS 应用层 | 服务表分发、DID 读写、会话、安全访问、DTC 和例程控制 |
| I2C、MPU6050 | 传感器采集、健康状态与诊断数据映射 |
| Python、pyserial、pytest | PC 串口桥、CAN-TP 编解码、诊断交互和主机回归测试 |
| STM32CubeIDE / CubeMX、GNU Arm GCC、ST-Link | 工程配置、编译、下载和调试 |

### 前置知识与学习顺序

面向已经接触过 STM32、希望进一步学习汽车诊断协议的读者，建议按下面的顺序补齐知识。

| 顺序 | 需要掌握的知识 | 在项目中对应的阅读目标 |
|---|---|---|
| 1 | C 指针、数组、结构体、函数指针、位运算与大小端 | 看懂服务配置表、字节编解码和缓冲区长度检查 |
| 2 | STM32 时钟、GPIO、UART、中断、HAL 与 SWD 调试 | 先跑通 PC 与 Tester 的 PING，再检查节点初始化 |
| 3 | CAN 帧 ID、DLC、位时序、过滤器、发送邮箱与终端电阻 | 理解一帧数据如何经过收发器、控制器和软件队列 |
| 4 | 状态机、环形队列、生产者/消费者与超时 | 理解 CAN-TP 为什么需要保存跨帧状态 |
| 5 | RTOS 任务优先级、阻塞/唤醒、队列、ISR 安全 API、临界区 | 理解中断如何通知任务，以及协议处理为何放在任务上下文 |
| 6 | UDS 的 SID、子功能、DID、正响应与 NRC | 从 `22 F1 90` 的请求与响应开始，逐步读懂诊断服务 |

无需先读完整标准再运行项目。可以先执行主机测试，跟踪一次 DID 读取，再阅读多帧和 RTOS 集成。DTC 是诊断故障码，NRC 是否定响应码，后文按具体服务介绍它们的用途。

## FreeRTOS 的应用情况

Mini-DCM 从早期裸机轮询逐步迁移到 FreeRTOS。当前公开目标工程保留 `MainFunction` 形式的协议接口，由任务驱动这些模块，使协议处理与外设中断、低频采样分开。

| 节点 / 任务 | 当前职责与触发方式 |
|---|---|
| F103 `dcmTask` | 较高优先级执行 `EcuApp_MainFunction`，处理 CAN 队列、CAN-TP、UDS 和会话；等待 CAN 接收通知并设置短超时，循环中显式让出 CPU |
| F103 `monitorTask` | 每轮调用 `SensorApp_MainFunction`，更新采样与健康状态，随后延时 100 ms |
| F103 `heartbeatTask` | 更新任务存活状态并驱动 LED，随后延时 500 ms |
| F407 桥接任务 `defaultTask` | UART / CAN 中断通知唤醒，10 ms 超时兜底；执行串口解析、CAN 收发队列和 TraceLink 处理 |
| F407 `testerHeartbeatTask` | 更新心跳计数与 LED，随后延时 500 ms |

F103 的 CAN RX 使用深度为 16 帧的 FreeRTOS 队列，中断通过 `xQueueSendFromISR` 入队，并通过任务通知唤醒诊断任务。F407 的串口接收和 CAN 收发保留固定缓冲区/队列，由桥接任务消费。中断只完成接收、入队和通知，UDS 解析与应答在任务中执行。

任务运行次数、最近运行时间、栈高水位和队列高水位用于观察任务与负载。源码中的 `osDelay` 和等待超时是调度参数，实际响应时间还受任务调度、总线负载与队列积压影响。入口见两目标工程的 `Src/main.c`，ECU 的任务职责封装见 [minidcm_task_runtime.c](firmware/targets/F103_ECU_Server/Src/minidcm_task_runtime.c)。

## CAN-TP 的实现逻辑

经典 CAN 一帧最多携带 8 字节。项目中的 CAN-TP 使用首字节中的协议控制信息识别帧类型，将较长的 UDS 报文拆分传输。状态机位于 [cantp.c](firmware/targets/F103_ECU_Server/Src/cantp.c)。

| 帧类型 | 本项目中的处理方式 |
|---|---|
| SF，Single Frame，单帧 | 不超过 7 字节的诊断报文直接编码发送；接收端移除长度信息后交给 UDS |
| FF，First Frame，首帧 | 携带总长度与前 6 字节；发送端进入等待流控状态，接收端建立重组上下文 |
| CF，Consecutive Frame，连续帧 | 每帧最多携带后续 7 字节；接收端检查 4-bit 序号并追加数据，达到总长度后交付完整报文 |
| FC，Flow Control，流控帧 | 接收端给出 Block Size（每批连续帧数量）和 STmin（连续帧最小间隔），发送端据此推进 |

发送状态按 `IDLE → WAIT_FC → SEND_CF → IDLE` 演进，达到 Block Size 后再次等待 FC。接收状态按 `IDLE → WAIT_CF → 完整报文可取` 演进；接收过程中按批补发 FC。`CanTp_MainFunction` 检查时间、发送进度与错误，避免用延时循环阻塞整个诊断任务。等待 FC 的 N_Bs 和等待 CF 的 N_Cr 均配置为 1000 ms，超时后结束对应传输并记录错误。

当前 ECU 传输层缓冲区上限为 **256 字节**，仅维护一个发送上下文和一个接收上下文；接收方向发出的 FC 使用 Block Size=2、STmin=10 ms。发送方向接受 CTS（允许继续发送），支持 `0x00..0x7F` 的毫秒级 STmin；WAIT / Overflow 流控和微秒级 STmin 未实现。ECU 向 UDS 交付的请求还受 8-bit 长度接口限制，响应缓冲区为 64 字节。这些是当前代码的实现范围。

PC 侧的 [cantp_codec.py](tools/pc_diag/cantp_codec.py) 与 [serial_bridge.py](tools/pc_diag/serial_bridge.py) 负责 Tester 一侧的分包、重组与请求交互；F407 主要承担 UART 与 CAN 帧之间的桥接。阅读时应区分 PC 侧传输逻辑与 ECU 侧 C 状态机。

## UDS 应用层协议的实现逻辑

ECU 取得完整诊断报文后，由 `EcuApp_ProcessUdsPayload` 刷新会话活动时间，再调用 `UdsServer_Dispatch`。分发器取第一个字节 SID（Service Identifier，服务标识符），查找 `uds_cfg.c` 中的 SID 与处理函数表。处理函数检查所需的长度、子功能与访问权限，调用数据或故障管理模块，生成响应后交回 CAN-TP。

```text
CAN 接收中断 → CanIf 队列 → CAN-TP 解包 / 重组
    → UDS 按 SID 查表 → 服务处理函数 → DID / DTC / Session / Security
    → UDS 响应 → CAN-TP 编码 / 分包 → CAN 发送队列
```

| SID | 已实现的应用层逻辑 |
|---|---|
| `0x10` | 切换默认、编程、扩展会话；非默认会话通过活动时间刷新 S3，5000 ms 无活动后回到默认会话 |
| `0x22` | 解析一个 DID（Data Identifier，数据标识符），由 `DidMgr_Read` 按配置读取设备信息、运行统计或传感器数据 |
| `0x2E` | 先检查安全解锁，再由 `DidMgr_Write` 检查 DID 可写属性和数据长度，更新对应值 |
| `0x27` | 请求 4 字节 Seed、提交 4 字节 Key、验证解锁；连续失败 3 次后锁定 10 秒 |
| `0x19` | 按状态掩码查询 DTC 数量或第一条匹配记录，支持子功能 `0x01/0x02` |
| `0x14` | 按代码中支持的 DTC 分组清除故障信息 |
| `0x31` | 支持 StartRoutine，按例程 ID 触发示例故障、清除故障、注入或恢复模拟传感器故障 |
| `0x3E` | 返回 TesterPresent 响应；公共请求入口同时刷新非默认会话的活动时间 |

例如，UDS 请求 `22 F1 90` 表示读取 DID `0xF190`。当前教学映射返回字符串 `F103`，正响应为 `62 F1 90 46 31 30 33`，恰好可放入一个 CAN-TP 单帧。`0xF190` 在这里用作设备信息演示，并未实现标准 VIN 数据内容。较长 DID 数据会使响应进入 CAN-TP 多帧流程。

支持的正响应使用对应服务的响应 SID；失败时，服务处理函数按具体错误构造 `7F <请求SID> <NRC>`。例如未解锁便写 DID 时返回 `7F 2E 33`，未知 SID 返回服务不支持。各处理函数的检查范围以源码为准，完整的子功能、会话权限矩阵和响应抑制行为尚未覆盖。

实现入口为 [uds_cfg.c](firmware/targets/F103_ECU_Server/Src/uds_cfg.c)、[uds_server.c](firmware/targets/F103_ECU_Server/Src/uds_server.c) 和 [did_mgr.c](firmware/targets/F103_ECU_Server/Src/did_mgr.c)。新增服务时注册 SID 与处理函数；新增 DID 时更新配置表及读写逻辑，再补对应边界验证。

## 系统与硬件

```text
PC --UART 9600 8N1--> F407 Tester --CAN 500 kbit/s--> F103 ECU + MPU6050
                          |
                          +--SPI2 DMA / READY--> TraceBox-BM（可选）
```

| 项目 | 配置 |
|---|---|
| ECU | STM32F103C8T6，8 MHz HSE，72 MHz 系统时钟 |
| Tester | STM32F407VET6，8 MHz HSE，168 MHz 系统时钟 |
| CAN 收发器 | SN65HVD230，3.3 V，11-bit CAN ID |
| CAN ID | 物理请求 `0x601`，响应 `0x602`，功能请求 `0x7DF` |
| MPU6050 | ECU I2C1，地址 `0x68` |
| 调试下载 | ST-Link / SWD |

引脚与初始化配置见两个目标工程的 `.ioc`、`Inc/main.h` 和 `Src/main.c`。UART 当前快照使用 **9600 baud**。首次接线前核对 CAN 总线两端终端电阻、共地和 3.3 V UART 电平。

## 协议与范围

CAN-TP 模块包含单帧、首帧、连续帧、流控、Block Size、STmin 和超时逻辑。UDS 服务表位于 `firmware/targets/F103_ECU_Server/Src/uds_cfg.c`，当前注册 `0x10/0x14/0x19/0x22/0x27/0x2E/0x31/0x3E`。各服务只实现源码中的子功能；`0x11 ECUReset` 尚未注册。协议子集适合实验复现，完整 ISO/AUTOSAR 一致性未经认证。

SecurityAccess 使用固定 4 字节 Seed 和 XOR Key，便于演示失败计数和锁定状态。它是公开教学算法，生产设备需要独立设计认证机制。

## 快速开始

Python 3.10 或更新版本；串口工具依赖 pyserial，主机测试使用 pytest，无需连接硬件。

```sh
python -m venv .venv
# 激活 .venv 后执行；Windows: .venv\Scripts\Activate.ps1
# Linux/macOS: . .venv/bin/activate
python -m pip install -r tools/requirements.txt
python -m pip install -r requirements-dev.txt
python -m pytest tests -q
python tools/audit_publication.py
```

查询串口和发送 PING：

```sh
python tools/pc_diag/serial_bridge.py list
python tools/pc_diag/serial_bridge.py ping --port COM9 --baudrate 9600 --payload hello --timeout-ms 1000
python tools/pc_diag/serial_bridge.py --help
```

将 `COM9` 替换为实际设备。其他串口名称可通过 Python CLI 传入。

## 构建固件

在 STM32CubeIDE 中选择 **File → Import → Existing Projects into Workspace**，导入 `firmware/targets/F103_ECU_Server` 和 `firmware/targets/F407_Tester`，使用 **Debug** 配置构建。项目来自 CubeIDE 1.4.0，保留原始 FreeRTOS 和 HAL 版本；更新 CubeMX 前先备份，生成器可能覆盖手写集成代码。Release 配置尚未验证。

也可使用 GNU Arm Embedded GCC 和 Python 直接构建，工具链需包含 newlib / nano.specs：

```sh
python tools/build_firmware.py
# 工具链未在 PATH 时：
python tools/build_firmware.py --toolchain-bin /path/to/arm-toolchain/bin
# 仅构建 ECU：
python tools/build_firmware.py --target F103_ECU_Server
```

输出位于 `build/<target>/`，包含 ELF、HEX、BIN 和 map。源文件列表从原工程 Debug 构建清单提取，固定保存在 `firmware/build_sources.json`。修改目标工程的编译单元时应同步更新该清单。

## 目录与联调

| 目录 | 内容 |
|---|---|
| `firmware/targets` | 两个完整 CubeIDE 工程和匹配的 HAL/CMSIS/FreeRTOS |
| `firmware/common` | 通用协议与运行时模块 |
| `firmware/ecu_server`、`firmware/tester` | 早期移植入口与模块组织 |
| `tools/pc_diag` | UART 桥接、CAN-TP 编解码与诊断 CLI |
| `tests` | Python 主机协议测试 |
| `docs/history` | 经过筛选的历史设计记录 |

可选联调项目：[TraceBox-BM](https://github.com/YifanNing6117/TraceBox-BM)。两个仓库并排放置时，TraceBox 的 `trace-demo` 默认读取 `../Mini-DCM/tools/pc_diag/serial_bridge.py`。本项目 Tester 已集成 `trace_link`，无需再次运行旧补丁。

## 验证与许可证

本快照的主机测试和构建结果见 [验证记录](docs/VALIDATION.md)。本次开源整理未连接硬件重测；历史文档中的硬件数字属于此前实验记录。原始串口日志、芯片 UID、个人路径、采购资料、浏览器截图和硬件 CAD 制造包均未纳入发布。

自有代码与文档采用 [MIT](LICENSE)。第三方文件保留原许可证，部分 Cube 生成文件采用 ST SLA0044，详情见 [第三方说明](THIRD_PARTY_NOTICES.md)。
