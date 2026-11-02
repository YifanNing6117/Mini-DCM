> 历史设计记录：原开发目录中的路径、阶段目标与实验记录按当时状态保留。当前构建、运行和验证入口见[仓库 README](../../README.md)。部分原始日志与规划资料未纳入公开快照。

# MiniDCM V3.1 MPU6050 Real Sensor Integration

## Scope

V3.1 replaces the V3.0 deterministic mock data provider with a real GY-521
MPU6050 provider on the F103 ECU. It preserves the existing 100 ms
`monitorTask -> SensorApp_MainFunction()` path, sensor DID contract
`F1C0..F1C6`, DTC `C00300`, and routine-based controlled-fault demonstration.

## Hardware contract

| MPU6050 pin | F103 ECU connection | Notes |
| --- | --- | --- |
| VCC | 3.3 V | Use the F103 3.3 V rail for this project. |
| GND | GND | Must share ground with ECU, Tester and CAN transceivers. |
| SCL | PB6 / I2C1_SCL | I2C standard mode, 100 kHz initial baseline. |
| SDA | PB7 / I2C1_SDA | Use module onboard pull-ups when present. |
| AD0 | unconnected / module default low | 7-bit I2C address is `0x68`. |
| INT, XDA, XCL | unconnected | Not required by the initial periodic polling provider. |

The board in use is the common GY-521 form factor. It exposes a 3.3 V logic
I2C interface. Do not deliberately apply 5 V pull-ups to PB6/PB7.

## Module boundary

```text
F103 CubeMX I2C1 / HAL
        ↓
firmware/ecu_server/src/mpu6050_hal_port.c
        ↓ callback transport
firmware/common/src/mpu6050.c
        ↓ provider interface
firmware/common/src/sensor_app.c
        ↓
F1C0..F1C6 / C00300 / UDS / CAN-TP
```

`mpu6050.c` contains no STM32 HAL headers. It validates `WHO_AM_I = 0x68`,
wakes the device through `PWR_MGMT_1`, selects ±2 g through `ACCEL_CONFIG`,
and reads the contiguous 14-byte acceleration/temperature register window.
The F103 port alone translates these transactions into `HAL_I2C_Mem_Read` and
`HAL_I2C_Mem_Write` calls with a bounded 20 ms timeout.

## Data conversion

The first real-data configuration uses the ±2 g range:

```text
accel_mg             = raw_accel * 1000 / 16384
temperature_deci_c   = raw_temperature * 10 / 340 + 365
```

All exported values remain signed big-endian integers through existing DID
serialization. No floating point is needed in the periodic runtime path.

## Fault behavior

- `WHO_AM_I` mismatch creates a sensor fault with device-ID and communication flags.
- I2C initialization/read failures create a communication fault.
- The first transition into a communication fault reports `MINIDCM_E_SENSOR_COMMUNICATION`, which DtcMgr maps to `C00300`.
- Successful subsequent reads restore the SensorApp health snapshot. DTC clearing remains explicit via `0x14` or the existing restore routine.
- Routines `0203` / `0204` remain controlled software fault injection and restore mechanisms; they complement physical unplug/reconnect testing.

## CubeMX and sync sequence

1. Enable **Connectivity -> I2C1** in the F103 `.ioc`; retain PB6/PB7 as I2C1_SCL/I2C1_SDA and select I2C standard mode at 100 kHz.
2. Generate code with **Keep User Code** enabled.
3. Run `tools/sync_v2_stage1_to_f103_cubeide.py`. It copies the common driver and F103 port, adds both modules to the generated make file, includes the port header, and registers the provider immediately after `MX_I2C1_Init()`.
4. Build, flash the F103, then run the acceptance sequence below.

The sync script intentionally refuses to run when `MX_I2C1_Init()` is absent, preventing a partial external-project update before CubeMX has generated I2C.

## Hardware acceptance

```text
1. 0x601 22 F1 C0  -> 62 F1 C0 02 01      (real source, healthy)
2. Read F1C1/F1C2/F1C3 while rotating the board; values change.
3. Read F1C5 twice several seconds apart; count increases.
4. In a horizontal placement, one axis is approximately ±1000 mg.
5. Disconnect VCC or SDA temporarily; F1C0 becomes 02 02 and 0x19 lists C00300.
6. Reconnect, verify new samples, clear DTC using 0x14 FF FF FF.
7. Run complete firmware regression: 16/16 PASS.
8. Run CAN-TP and Runtime tests while sampling to prove no regression.
```

## Current status

Hardware acceptance complete on the physical F103 + GY-521 module:

```text
F1C0 -> 62 F1 C0 02 01                 real provider healthy
F1C1/F1C2/F1C3                        live orientation-dependent acceleration
F1C4                                  live sensor temperature
F1C5                                  continuously increasing 100-ms samples
MPU VCC disconnect -> F1C0=02 02, F1C6=01, DTC C00300
Reconnect/reset -> F1C0=02 01; 0x14 clear; 0x19 count=0
Complete firmware regression with sensor active: 16/16 PASS
```

The V3.1 real sensor load is now a verified implementation, not a future
target.
