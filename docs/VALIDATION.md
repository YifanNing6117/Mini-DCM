# 开源快照验证

验证日期：2026-10-27。所有命令在本次整理后的仓库副本中运行。

| 检查 | 结果 |
|---|---|
| `python -m pytest tests -q` | 15 项通过；Python 3.9.16 / pytest 8.3.5 |
| `python tools/build_firmware.py --toolchain-bin <toolchain-bin>` | F103 / F407 均通过；Python 3.12.14，GNU Arm Embedded GCC 7-2018-q2 |
| `python tools/audit_publication.py` | 370 个待发布文件，0 项发现；未纳入历史 Git 对象 |
| 硬件复测 | 本次未连接开发板 |
| GitHub Actions | 工作流已提供，尚未上传运行 |

构建大小来自 `arm-none-eabi-size`：

| 目标 | text | data | bss | 静态 RAM（data + bss） |
|---|---:|---:|---:|---:|
| F103_ECU_Server | 40,032 B | 60 B | 12,516 B | 12,576 B |
| F407_Tester | 29,536 B | 24 B | 19,968 B | 19,992 B |

F103 当前 FreeRTOS 构建超过早期 10 KB RAM 目标，静态 RAM 仍小于芯片的 20 KB。此表包含静态预留的 FreeRTOS heap；运行时栈高水位、任务堆用量和峰值负载需要实板验证，不能仅由链接大小推断。

源文件清单来自原目标工程的 Debug 构建，启动文件、HAL、FreeRTOS port 和 heap 实现均随仓库提供。命令行入口重建全部编译单元，不读取原工作目录的对象文件或 Debug 目录。Release 配置和更新版本的工具链未在本轮验证。

本轮首先在受限沙箱中尝试验证，旧编译器启动、临时目录及 localhost 访问受到环境限制；随后在本机正常权限下完成上述成功测试。该环境问题未通过更改应用行为规避。
