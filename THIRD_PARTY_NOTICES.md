# 第三方组件与许可证

根目录 MIT 许可证适用于项目自有代码和文档。第三方源码、生成文件中的版权与许可证声明继续有效。

| 组件 | 来源与版本 | 许可证位置 |
|---|---|---|
| STM32F1 HAL / CMSIS | STM32Cube FW_F1 V1.8.4，本地目标工程使用的源文件 | `firmware/targets/F103_ECU_Server/Drivers` 内的 License.md / LICENSE.txt |
| STM32F4 HAL / CMSIS | STM32Cube FW_F4 V1.25.2，本地目标工程使用的源文件 | `firmware/targets/F407_Tester/Drivers` 内的 License.md / LICENSE.txt |
| FreeRTOS | ECU V10.0.1；Tester V10.2.1；Amazon 与原贡献者 | 各目标 `Middlewares/Third_Party/FreeRTOS/License/license.txt` 及源码头部 |
| Cube 生成的启动、系统与板级文件 | STMicroelectronics；依各文件头部声明 | BSD-3-Clause 或 ST SLA0044；完整文本见 `LICENSES` |

组件许可证补自与目标工程 `.ioc` 版本一致的 STM32Cube 软件包。个别文件年代或声明可能不同，以随文件保留的声明为准；未将第三方代码改授 MIT。

ST SLA0044 包含在 ST 器件上使用的条件，因此完整固件分发包含有使用条件的厂商代码。项目自有协议模块与 PC 工具的 MIT 授权不改变这些条件。

上游入口：[STM32CubeF1](https://github.com/STMicroelectronics/STM32CubeF1)、[STM32CubeF4](https://github.com/STMicroelectronics/STM32CubeF4)、[FreeRTOS](https://github.com/FreeRTOS/FreeRTOS-Kernel)、[SLA0044](https://www.st.com/SLA0044)。
