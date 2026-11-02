# Mini-DCM

基于 STM32F103 ECU 与 STM32F407 Tester 的 UDS over CAN 诊断教学项目，包含 CAN 接口、CAN 传输层、UDS 服务、会话与安全访问、DID、DTC 和 MPU6050 数据接入。适合学习诊断协议分层、FreeRTOS 任务和队列，以及双节点联调。

本发布快照包含完整 CubeIDE 工程和独立协议模块。`firmware/targets` 是当前可构建的 FreeRTOS 工程；`firmware/common` 保留通用模块，供阅读和移植。早期裸机设计的演进记录位于 `docs/history`，当前行为以目标工程源码为准。

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
