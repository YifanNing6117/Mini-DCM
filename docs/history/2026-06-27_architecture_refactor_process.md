> 历史设计记录：原开发目录中的路径、阶段目标与实验记录按当时状态保留。当前构建、运行和验证入口见[仓库 README](../../README.md)。部分原始日志与规划资料未纳入公开快照。

# 2026-06-27 Mini-DCM architecture refactor process

This document records the refactor story after the first UDS-over-CAN hardware slice was already working. It is intended for interview review: what was built first, why it was moved, where it was moved, and how each step was validated.

## Starting point

The first working hardware chain was:

```text
PC Python
  -> F407 Tester UART protocol
  -> F407 CAN1
  -> CAN bus
  -> F103 ECU CAN RX interrupt
  -> CAN-TP Single Frame decode
  -> UDS dispatch
  -> DID read / negative response
  -> CAN-TP Single Frame encode
  -> CAN response
  -> F407 CAN poll
  -> PC result
```

This proved the full path, but the F103 code still had too much logic in `main.c` and initially too much work in the CAN RX interrupt callback. The next target was to keep the verified behavior and move the code toward the planned layered architecture.

## Step 1: CanIf extraction

Initial location:

```text
firmware/targets/F103_ECU_Server\Src\main.c
```

The first CAN responder and UDS Single Frame code directly used HAL CAN APIs from `main.c`.

Refactor:

```text
firmware/common/inc/can_if.h
firmware/common/src/can_if.c
```

Responsibilities moved into `CanIf`:

- CAN filter setup for request ID `0x601`.
- CAN start and notification activation.
- FIFO0 read wrapper into `CanIf_FrameType`.
- standard CAN transmit wrapper for response ID `0x602`.
- basic TX/error counters.

External CubeIDE synchronization:

```text
firmware/targets/F103_ECU_Server\Inc\can_if.h
firmware/targets/F103_ECU_Server\Src\can_if.c
```

Result:

`main.c` no longer called raw HAL CAN transmit directly for ECU responses. The hardware behavior stayed unchanged.

## Step 2: CanIf RX queue

Problem:

After the first `CanIf` extraction, the CAN RX interrupt still did too much:

```text
HAL_CAN_RxFifo0MsgPendingCallback
  -> CanIf_ReadRxFifo0
  -> CAN-TP decode
  -> UDS dispatch
  -> CAN-TP encode
  -> CanIf_Transmit
```

This violated the project rule:

```text
Interrupts only do minimal work:
  - read hardware data
  - enqueue it
  - update counters/status
Complex protocol processing runs in the main loop.
```

Refactor:

`CanIf` gained a 16-frame RX ring queue:

```c
CanIf_StatusType CanIf_RxIndication(const CanIf_FrameType *frame);
CanIf_StatusType CanIf_Receive(CanIf_FrameType *frame);
uint32_t CanIf_GetRxCount(void);
uint32_t CanIf_GetRxOverflowCount(void);
```

New flow:

```text
HAL_CAN_RxFifo0MsgPendingCallback
  -> CanIf_ReadRxFifo0
  -> CanIf_RxIndication

main loop
  -> dequeue with CanIf_Receive
  -> run diagnostic processing
```

Hardware validation:

After F103 Build/Flash and resetting both boards, the following checks passed on F407 `COM16`:

```text
info     -> Mini-DCM Tester F407 USART1
22 F1 90 -> 62 F1 90 46 31 30 33
3E 00    -> 7E 00
22 F1    -> 7F 22 13
22 F1 91 -> 7F 22 31
99       -> 7F 99 11
```

CAN TX diagnostics stayed healthy:

```text
canTxAccepted: true
txFreeMailboxes: 2
halCanError: 0x00000000
```

## Step 3: EcuApp extraction

Problem:

After the RX queue was introduced, `main.c` still contained ECU application logic:

```text
ECU_MainFunction
ECU_CAN_SendResponse
raw echo fallback
CAN-TP Single Frame decode/encode calls
UDS dispatch calls
```

This made `main.c` more than a generated startup/scheduler file.

Refactor:

```text
firmware/common/inc/ecu_app.h
firmware/common/src/ecu_app.c
```

External CubeIDE synchronization:

```text
firmware/targets/F103_ECU_Server\Inc\ecu_app.h
firmware/targets/F103_ECU_Server\Src\ecu_app.c
```

New `main.c` role:

```text
initialize HAL/CubeMX peripherals
initialize CanIf
call EcuApp_MainFunction in while(1)
read CAN FIFO and enqueue frames in interrupt callback
```

New `EcuApp` role:

```text
dequeue request frames from CanIf
preserve raw CAN echo fallback for non-CAN-TP debug frames
decode CAN-TP Single Frame
dispatch UDS
encode CAN-TP Single Frame response
send response through CanIf
```

Validation status:

Host-side Python protocol tests still pass. F103 hardware regression is the next required validation after Build/Flash.

## Current architecture after these steps

```text
F103 main.c
  -> CanIf_Init / CanIf_Start
  -> EcuApp_MainFunction
  -> HAL_CAN_RxFifo0MsgPendingCallback only queues frames

EcuApp
  -> CanIf_Receive
  -> CAN-TP Single Frame handling
  -> UdsServer_Dispatch
  -> CanIf_Transmit

UdsServer
  -> service dispatch
  -> DidMgr_Read

DidMgr
  -> DID table/read handlers
```

## Step 4: CanTp wrapper around the verified Single Frame codec

Initial location:

```text
firmware/common/inc/cantp_sf.h
firmware/common/src/cantp_sf.c
```

Why it was written there first:

The first diagnostic slice only needed CAN-TP Single Frame. A small `cantp_sf` module was faster to validate because it did exactly two things:

```text
CAN data -> UDS payload
UDS payload -> CAN data
```

This was enough for:

```text
22 F1 90 -> 62 F1 90 46 31 30 33
3E 00    -> 7E 00
negative responses
```

Problem:

After `EcuApp` extraction, the application still directly included `cantp_sf.h`. That made the application aware of the temporary Single Frame-only implementation. Later, when First Frame / Consecutive Frame / Flow Control are added, that direct dependency would force application-level changes.

Refactor:

```text
firmware/common/inc/cantp.h
firmware/common/src/cantp.c
```

New wrapper API:

```c
CanTpSf_StatusType CanTp_DecodeSingleFrame(const uint8_t *can_data, uint8_t can_dlc, CanTp_FrameViewType *frame);
CanTpSf_StatusType CanTp_EncodeSingleFrame(const uint8_t *payload, uint8_t payload_len, uint8_t *can_data);
```

Current logic:

`CanTp` currently delegates to the verified `CanTpSf` codec. It does not change runtime behavior. Its purpose is to create the formal transport-layer boundary:

```text
EcuApp
  -> CanTp
      -> CanTpSf
```

External CubeIDE synchronization:

```text
firmware/targets/F103_ECU_Server\Inc\cantp.h
firmware/targets/F103_ECU_Server\Src\cantp.c
firmware/targets/F103_ECU_Server\Src\ecu_app.c
```

Build metadata:

The current Debug build list was updated so `cantp.c` participates in the F103 build:

```text
firmware/targets/F103_ECU_Server\Debug\Src\subdir.mk
```

Validation status:

Host-side Python protocol tests still pass.

Hardware validation:

After F103 Build/Flash, the first attempt was run without a clean board reset. The first request succeeded, then later requests timed out and eventually filled the F407 TX mailboxes. This was treated as a false validation failure caused by stale CAN peripheral state, matching the earlier bring-up lesson.

Recovery:

```text
reset F407
reset F103
rerun info first
rerun UDS regression
```

Final hardware regression on `COM16` passed:

```text
info     -> Mini-DCM Tester F407 USART1
22 F1 90 -> 62 F1 90 46 31 30 33
3E 00    -> 7E 00
22 F1    -> 7F 22 13
22 F1 91 -> 7F 22 31
99       -> 7F 99 11
```

All requests completed with:

```text
pollAttempts: 1
canTxAccepted: true
txFreeMailboxes: 2
halCanError: 0x00000000
```

Conclusion:

The `CanTp` wrapper introduced a formal transport-layer boundary without changing behavior. `EcuApp` no longer depends directly on the Single Frame-only implementation, which keeps the application layer cleaner for future multi-frame support.

## Step 5: UDS service table and Session Manager

Initial location:

```text
firmware/common/src/uds_server.c
```

Before this step, `UdsServer_Dispatch` selected services with hard-coded branches:

```text
if SID == 0x22 -> ReadDataByIdentifier
if SID == 0x3E -> TesterPresent
else -> negative response 0x11
```

This was acceptable for the first vertical slice, but it does not tell a strong architecture story. UDS services are naturally table-driven: each SID maps to a handler function, and adding a service should not keep growing the dispatcher body.

Refactor:

```text
firmware/common/src/uds_server.c
```

The dispatcher now uses a service table:

```c
typedef void (*UdsServer_ServiceHandlerType)(...);

typedef struct
{
    uint8_t sid;
    UdsServer_ServiceHandlerType handler;
} UdsServer_ServiceConfigType;
```

Current service table:

```text
0x10 DiagnosticSessionControl
0x22 ReadDataByIdentifier
0x3E TesterPresent
```

New module:

```text
firmware/common/inc/session_mgr.h
firmware/common/src/session_mgr.c
```

The first `SessionMgr` version stores the active session and accepts:

```text
0x01 DefaultSession
0x02 ProgrammingSession
0x03 ExtendedSession
```

New UDS behavior:

```text
10 01 -> 50 01 00 32 01 F4
10 02 -> 50 02 00 32 01 F4
10 03 -> 50 03 00 32 01 F4
10 99 -> 7F 10 12
10    -> 7F 10 13
```

The timing bytes are placeholders that keep the response within the current CAN-TP Single Frame limit:

```text
00 32 = P2 server timing placeholder
01 F4 = P2* server timing placeholder
```

External CubeIDE synchronization:

```text
firmware/targets/F103_ECU_Server\Inc\session_mgr.h
firmware/targets/F103_ECU_Server\Src\session_mgr.c
firmware/targets/F103_ECU_Server\Src\uds_server.c
```

Build metadata:

```text
firmware/targets/F103_ECU_Server\Debug\Src\subdir.mk
```

Validation status:

Host-side Python protocol tests still pass.

Hardware validation:

After F103 Build/Flash and reset, F407 `COM16` first returned:

```text
info -> Mini-DCM Tester F407 USART1
```

This only validates the F407 Tester UART protocol, not the F103 ECU firmware. F103 firmware validation was confirmed by sending new `0x10` UDS requests over CAN:

```text
10 01 -> 50 01 00 32 01 F4
10 03 -> 50 03 00 32 01 F4
10 99 -> 7F 10 12
```

All three requests completed with:

```text
pollAttempts: 1
canTxAccepted: true
txFreeMailboxes: 2
halCanError: 0x00000000
```

Conclusion:

The new Session Manager and table-driven UDS dispatcher are active on the F103 ECU. The test also clarified a useful validation rule: `info` proves the F407 Tester is alive, while a new UDS behavior such as `10 01` proves the F103 ECU firmware update.

## Step 6: S3 timeout and observable session DID

Initial state:

`SessionMgr` could store the active diagnostic session, but it did not yet model the UDS S3 timeout. That meant `10 03` could switch to ExtendedSession, but there was no automatic return to DefaultSession after tester inactivity.

Refactor:

```text
firmware/common/inc/session_mgr.h
firmware/common/src/session_mgr.c
```

New responsibilities:

```c
void SessionMgr_MainFunction(uint32_t now_ms);
void SessionMgr_OnUdsActivity(uint32_t now_ms);
```

Runtime behavior:

```text
non-default session + no UDS activity for 5000 ms -> DefaultSession
any valid CAN-TP Single Frame UDS request -> refresh S3 activity timestamp
```

Integration point:

```text
F103 main.c
  -> EcuApp_MainFunction(HAL_GetTick())

EcuApp
  -> SessionMgr_MainFunction(now_ms)
  -> on valid UDS frame: SessionMgr_OnUdsActivity(now_ms)
```

Why the tick is passed from `main.c`:

`SessionMgr` should not call HAL directly. Passing `HAL_GetTick()` from the top-level loop keeps the session module portable and easier to unit test later.

Observable DID:

To make S3 timeout visible through the existing UDS test path, `DidMgr` gained DID `0xF186`:

```text
22 F1 86 -> 62 F1 86 <active_session>
```

Example validation sequence after flashing:

```text
22 F1 86 -> 62 F1 86 01
10 03    -> 50 03 00 32 01 F4
22 F1 86 -> 62 F1 86 03
wait > 5 seconds without UDS requests
22 F1 86 -> 62 F1 86 01
```

Files changed:

```text
firmware/common/inc/session_mgr.h
firmware/common/src/session_mgr.c
firmware/common/inc/ecu_app.h
firmware/common/src/ecu_app.c
firmware/common/src/did_mgr.c
firmware/targets/F103_ECU_Server\Src\main.c
```

Validation status:

Host-side Python protocol tests still pass.

Hardware validation:

The basic seed/key path passed:

```text
27 01             -> 67 01 12 34 56 78
27 02 B7 91 F3 DD -> 67 02
27 02 00 00 00 00 -> 7F 27 35
```

The lockout path also passed:

```text
wrong key attempt 2 -> 7F 27 35
wrong key attempt 3 -> 7F 27 35
locked seed request -> 7F 27 36
after lock timeout  -> 67 01 12 34 56 78
```

Conclusion:

The first SecurityAccess loop is complete: the ECU can issue a seed, accept a matching key, reject invalid keys, count failures, enter lockout after three failures, and recover after the lockout time.

Debug note:

The first hardware check showed:

```text
22 F1 86 -> 62 F1 86 01
10 03    -> 50 03 00 32 01 F4
22 F1 86 -> 62 F1 86 01
```

This means the `0x10` service handler produced a positive response, but the session state was not observable as retained through DID `F186`. This was a useful design lesson: a positive service response is not enough to prove the internal state transition happened correctly.

Fix:

`SessionMgr_SetSession()` now refreshes the S3 activity timestamp when the session changes:

```text
SessionMgr_SetSession
  -> update current session
  -> refresh last_activity_ms from the latest main-loop tick
```

This keeps the S3 timer ownership inside `SessionMgr`, instead of relying only on the caller to refresh activity before dispatch.

Follow-up correction:

The first fix still left the timing path implicit because `SessionMgr_SetSession()` used an internally cached `now_ms`. The interface was tightened to make the tick explicit:

```c
SessionMgr_StatusType SessionMgr_SetSession(uint8_t session, uint32_t now_ms);
void UdsServer_Dispatch(const uint8_t *request, uint8_t request_len, uint32_t now_ms, ...);
```

The resulting path is now:

```text
main.c HAL_GetTick()
  -> EcuApp_MainFunction(now_ms)
  -> UdsServer_Dispatch(..., now_ms, ...)
  -> SessionMgr_SetSession(session, now_ms)
```

This is easier to reason about than hidden shared time state. It also forces all call sites to be rebuilt because the function signatures changed, reducing the chance of accidentally testing stale object files.

Final hardware validation:

```text
22 F1 86 -> 62 F1 86 01
10 03    -> 50 03 00 32 01 F4
22 F1 86 -> 62 F1 86 03
wait > 5 seconds without UDS traffic
22 F1 86 -> 62 F1 86 01
```

Conclusion:

The S3 timeout path is now complete for the first version: DiagnosticSessionControl can enter ExtendedSession, DID `F186` exposes the current session, and inactivity returns the ECU to DefaultSession.

## Step 7: SecurityAccess minimum state machine

Goal:

Add the first version of UDS `0x27 SecurityAccess`, focused on demonstrating the protocol state machine rather than cryptographic strength.

New module:

```text
firmware/common/inc/security_mgr.h
firmware/common/src/security_mgr.c
```

Why a separate module:

SecurityAccess has state that should not live in `uds_server.c`:

```text
current unlock state
failed key attempts
lockout deadline
seed/key algorithm
```

Keeping this in `SecurityMgr` lets `UdsServer` remain a protocol dispatcher and response builder.

First-version algorithm:

```text
seed = 12 34 56 78
key  = seed byte XOR A5
     = B7 91 F3 DD
```

This is intentionally simple for the first project version. The important architecture point is that the algorithm is hidden behind `SecurityMgr`, so it can later be replaced without changing the UDS service handler.

UDS behavior:

```text
27 01             -> 67 01 12 34 56 78
27 02 B7 91 F3 DD -> 67 02
27 02 wrong key   -> 7F 27 35
after 3 failures  -> locked for 10000 ms
locked request    -> 7F 27 36
unsupported subfn -> 7F 27 12
bad length        -> 7F 27 13
```

Files changed:

```text
firmware/common/inc/security_mgr.h
firmware/common/src/security_mgr.c
firmware/common/src/uds_server.c
firmware/targets/F103_ECU_Server\Inc\security_mgr.h
firmware/targets/F103_ECU_Server\Src\security_mgr.c
firmware/targets/F103_ECU_Server\Src\uds_server.c
firmware/targets/F103_ECU_Server\Debug\Src\subdir.mk
```

Build note:

`security_mgr.c` was added to the current F103 Debug build metadata so CubeIDE includes it in the firmware.

Validation status:

Host-side Python protocol tests still pass. Hardware validation is required after F103 Build/Flash.

## Step 8: WriteDataByIdentifier gated by SecurityAccess

Goal:

Add UDS `0x2E WriteDataByIdentifier` as the first protected write operation. This connects the already implemented `SecurityMgr` unlock state to an observable ECU behavior.

Initial location:

Before this step, `DidMgr` only exposed:

```c
uint8_t DidMgr_Read(uint16_t did, uint8_t *data, uint8_t *data_len, uint8_t max_len);
```

`UdsServer` had a service table, but no `0x2E` handler. The only mutable diagnostic state was inside `SessionMgr` and `SecurityMgr`.

Why this split:

Write handling has two different responsibilities:

```text
UdsServer
  - validate UDS request format
  - enforce SecurityAccess gate
  - build positive and negative UDS responses

DidMgr
  - own DID storage and read/write permissions
  - validate DID-specific data length
```

Keeping DID write rules in `DidMgr` prevents `UdsServer` from becoming a hard-coded DID storage module.

New interface:

```c
uint8_t DidMgr_Write(uint16_t did, const uint8_t *data, uint8_t data_len);
```

New observable DID:

```text
DID F187: one-byte writable RAM value
default read: 22 F1 87 -> 62 F1 87 00
write:        2E F1 87 5A -> 6E F1 87
readback:     22 F1 87 -> 62 F1 87 5A
```

Security behavior:

```text
2E F1 87 5A before unlock -> 7F 2E 33
27 01                     -> 67 01 12 34 56 78
27 02 B7 91 F3 DD         -> 67 02
2E F1 87 5A after unlock  -> 6E F1 87
```

Other negative paths:

```text
2E F1 87          -> 7F 2E 13
2E F1 87 01 02    -> 7F 2E 13
2E F1 90 5A       -> 7F 2E 31
```

Files changed:

```text
firmware/common/inc/did_mgr.h
firmware/common/src/did_mgr.c
firmware/common/src/uds_server.c
firmware/targets/F103_ECU_Server\Inc\did_mgr.h
firmware/targets/F103_ECU_Server\Src\did_mgr.c
firmware/targets/F103_ECU_Server\Src\uds_server.c
```

Validation status:

Host-side Python protocol tests still pass:

```text
python -m pytest .\tests\test_minidcm_uart_link.py -q
5 passed
```

Hardware validation:

After F103 Build/Flash and running through F407 `COM16`, the full protected write path passed:

```text
2E F1 87 5A         -> 7F 2E 33
27 01               -> 67 01 12 34 56 78
27 02 B7 91 F3 DD   -> 67 02
2E F1 87 5A         -> 6E F1 87
22 F1 87            -> 62 F1 87 5A
```

All requests reported `pollAttempts: 1`, `canTxAccepted: true`, `txFreeMailboxes: 2`, `halCanState: 0x02`, and `halCanError: 0x00000000`.

## Step 9: Functional addressing receive path

Goal:

Close the resume-description gap for functional addressing ID `0x7DF`. The project already used physical request ID `0x601` and response ID `0x602`; this step lets the ECU also accept functional UDS requests on `0x7DF`.

Initial location:

`CanIf` configured the F103 acceptance filter for only `CANIF_ECU_REQUEST_ID`:

```text
0x601 physical request
```

`EcuApp_MainFunction()` also only dispatched frames whose ID was `0x601`.

Refactor:

Added a formal functional request constant:

```c
#define CANIF_FUNCTIONAL_REQUEST_ID 0x7DFu
```

Updated the F103 CAN filter mask so FIFO0 accepts both:

```text
0x601 physical request
0x7DF functional request
```

Updated `EcuApp_MainFunction()` to dispatch either ID through the same CAN-TP Single Frame and UDS server path. Responses still use physical response ID `0x602`, matching the existing F407 polling path.

Follow-up correction:

The first hardware check showed that F407 accepted and transmitted `0x7DF`, but no ECU response was returned:

```text
uds --id 0x7DF --hex "22 F1 90" -> timeout
uds --id 0x601 --hex "22 F1 90" -> 62 F1 90 46 31 30 33
```

This proved the bus and physical request path were healthy, but the functional request was still being dropped on the F103 side. The remaining old gate was in the external F103 `main.c` CAN RX interrupt callback:

```text
HAL_CAN_RxFifo0MsgPendingCallback
  -> CanIf_ReadRxFifo0
  -> only enqueue if rx_frame.id == CANIF_ECU_REQUEST_ID
```

That callback was updated to enqueue either `CANIF_ECU_REQUEST_ID` or `CANIF_FUNCTIONAL_REQUEST_ID`. This keeps the interrupt policy unchanged: it still only reads FIFO0 and enqueues accepted diagnostic frames.

Files changed:

```text
firmware/common/inc/can_if.h
firmware/common/src/can_if.c
firmware/common/src/ecu_app.c
firmware/targets/F103_ECU_Server\Inc\can_if.h
firmware/targets/F103_ECU_Server\Src\can_if.c
firmware/targets/F103_ECU_Server\Src\ecu_app.c
firmware/targets/F103_ECU_Server\Src\main.c
```

Validation status:

Host-side Python protocol tests still pass:

```text
python -m pytest .\tests\test_minidcm_uart_link.py -q
5 passed
```

Final hardware validation:

```text
serial_bridge.py uds --id 0x7DF --hex "22 F1 90" -> 62 F1 90 46 31 30 33
serial_bridge.py uds --id 0x7DF --hex "3E 00"    -> 7E 00
serial_bridge.py uds --id 0x601 --hex "22 F1 90" -> 62 F1 90 46 31 30 33
```

All three requests passed on F407 `COM16` with `pollAttempts: 1`, `canTxAccepted: true`, `txFreeMailboxes: 2`, `halCanState: 0x02`, and `halCanError: 0x00000000`.

## Short interview story

The project was first developed as a vertical slice instead of designing every layer upfront. The first priority was to prove the hardest uncertainty: real PC-to-F407-to-CAN-to-F103 hardware communication and a real UDS response. After that path worked, the code was refactored step by step into layers. This avoided abstracting untested assumptions and gave a regression command set after each architectural move.

## Step 10: SensorApp application-load preparation (V3.0)

Initial location:

```text
The FreeRTOS monitorTask only updated task-health counters.
```

Why it was changed:

The protocol stack and FreeRTOS scheduler had been validated, but the ECU had
no application-owned periodic input. A sensor application is needed to prove
that the framework carries a real workload rather than diagnostics only.

New location and interface:

```text
firmware/common/inc/sensor_app.h
firmware/common/src/sensor_app.c

monitorTask
  -> MiniDcm_TaskRuntime_MonitorOnce()
  -> SensorApp_MainFunction(now_ms)
```

The initial provider is intentionally deterministic mock data because the
existing MPU6050 module is not yet physically available. The stable output
interface is a `SensorApp_SnapshotType` containing source, health, X/Y/Z
acceleration, temperature, sample counter, and fault flags.

Diagnostic integration:

```text
F1C0..F1C6 -> sensor state and values
31 01 02 03 -> inject controlled sensor communication fault
31 01 02 04 -> restore normal state
0x0500      -> C00300 sensor communication DTC
```

This preserves module boundaries: task runtime schedules the application,
SensorApp owns sampling state, DidMgr only serializes the exposed snapshot,
and DtcMgr owns DTC storage. When the MPU6050 arrives, an I2C BSP replaces the
mock provider while the RTOS, UDS, DID, and DTC contracts remain intact.

## Step 12: Real MPU6050 provider preparation (V3.1)

Initial location:

```text
sensor_app.c generated deterministic mock acceleration and temperature data.
```

Why it was changed:

The GY-521 MPU6050 module became available. The V3.0 mock had already proven
the application-to-diagnostic contract, so the next step is to replace only
the data provider and preserve all validated DID, DTC, RTOS and UDS behavior.

New locations and interfaces:

```text
firmware/common/inc/mpu6050.h
firmware/common/src/mpu6050.c
firmware/ecu_server/inc/mpu6050_hal_port.h
firmware/ecu_server/src/mpu6050_hal_port.c

CubeMX I2C1 -> HAL port -> generic MPU6050 driver -> SensorApp provider
```

The generic driver owns register protocol and integer conversion. The F103
port owns HAL I2C transactions and the `hi2c1` binding. SensorApp owns health,
snapshot publication and DTC error reporting. This preserves the no-cross-layer
rule: DID reads never directly issue I2C transfers.

Pre-hardware verification:

```text
gcc -fsyntax-only: mpu6050.c and sensor_app.c passed
pytest tests: 13 passed
sync script syntax check: passed
```

Hardware acceptance remains pending I2C1 CubeMX generation, F103 flash and
the V3.1 physical test matrix documented in
`docs/MiniDCM_V3_1_MPU6050_Integration_Design.md`.

## Step 11: Project knowledge and interview-delivery consolidation

Why it was changed:

The project had accumulated source-level modules, test scripts, staged design
notes, and real-hardware acceptance records. That was sufficient for ongoing
development, but not sufficient for an interviewer or a future maintainer to
quickly understand the system boundary, evidence, and remaining work.

New deliverables:

```text
docs/MiniDCM_Complete_Project_Description.md
docs/MiniDCM_Interview_Learning_Guide.md
output/pdf/MiniDCM_Complete_Project_Description.pdf
output/pdf/MiniDCM_Interview_Learning_Guide.pdf
tools/generate_minidcm_docs_pdf.py
```

The project description consolidates architecture, implemented UDS services,
FreeRTOS migration, queue behavior, bidirectional CAN-TP, observability,
validation evidence, resume wording, and implementation boundaries. The
learning guide converts the same evidence into a three-minute explanation,
protocol concepts, common interview questions, and a demonstration checklist.

Verification:

```text
generate_minidcm_docs_pdf.py completed successfully.
Both generated PDFs were rendered and visually checked for Chinese font,
tables, headings, code blocks, and page numbering.

## Step 12: Preserve FreeRTOS stack diagnostics across CubeMX regeneration

Why it was changed:

After I2C1 was generated for the MPU6050 integration, the F103 project linked
with an undefined `uxTaskGetStackHighWaterMark`. CubeMX regenerated
`FreeRTOSConfig.h` without enabling the optional FreeRTOS API used by the
already-existing MiniDCM task stack-watermark diagnostics.

Change:

`tools/sync_v2_stage1_to_f103_cubeide.py` now writes the following definition
inside CubeMX's preserved `USER CODE BEGIN Defines` block:

```c
#define INCLUDE_uxTaskGetStackHighWaterMark 1
```

This is configuration-only: it restores the FreeRTOS API implementation and
does not alter scheduling, MPU6050 I2C access, or the diagnostic protocol.
Keeping it in the sync script makes the fix repeatable after later `.ioc`
regeneration.

Verification pending:

```text
Re-run the F103 sync script, build and flash F103, then run the 16-step
firmware regression and the V3.1 MPU6050 acceptance matrix.
```

Follow-up diagnosis:

The first V3.1 F103 build succeeded but did not answer UDS. Inspection found
that CubeMX had also reset `configTOTAL_HEAP_SIZE` from the validated 8192 B
to 3072 B. The DCM, monitor and heartbeat task stacks alone require 3584 B,
before task control blocks and the 16-frame CAN RX queue. As a result the DCM
task could not be created, while CAN hardware could still intermittently ACK
frames. The sync script now restores both the optional stack-watermark API and
the 8192 B heap setting.

## Step 13: Replace ambiguous dual-address CAN filter with exact filter banks

Diagnosis:

With the F103 firmware alive and the CAN physical layer again ACKing F407
requests, a breakpoint in `HAL_CAN_RxFifo0MsgPendingCallback` did not fire.
The received frames were therefore being rejected before FIFO0 / the ISR.

Change:

`CanIf_Start()` now programs two explicit 32-bit ID-mask filters:

```text
Filter bank 0: exact standard ID 0x601 (physical UDS request)
Filter bank 1: exact standard ID 0x7DF (functional UDS request)
```

The former single-mask expression was compact but difficult to audit. The new
configuration makes the physical and functional receive policy explicit while
keeping address validation in the existing CAN callback and ECU application.

Verification pending:

```text
Rebuild/flash F103, confirm the CAN RX callback breakpoint is hit by 0x601,
then run the 16-step full firmware regression.
```

Bring-up follow-up:

The explicit filters still did not trigger FIFO0 on the target. For the next
hardware isolation build, `CanIf_Start()` uses a temporary all-pass bxCAN
filter. The ISR and `EcuApp_MainFunction` continue to accept only 0x601 and
0x7DF, so UDS address handling is unchanged. This isolates hardware filter
programming from CAN IRQ / RTOS queue behavior; exact hardware filters remain
in the source behind `CANIF_USE_EXACT_RX_FILTER` for restoration after the
board-level diagnosis.

## Step 14: F103-local bxCAN loopback isolation test

Context:

After the V3.1 MPU6050 CubeMX regeneration, the F103 FreeRTOS DCM and
heartbeat tasks were verified by debugger breakpoints.  Its bxCAN controller
also reported `INAK=0`, `ESR=0`, and no HAL error while idle.  However, an
F407 UDS request did not enter `USB_LP_CAN1_RX0_IRQHandler`, while the tester
eventually reported CAN Bus-Off.  This proves only that no external frame
reached F103 FIFO0; it does not establish that either SN65HVD230 module is
faulty.

Change:

`tools/sync_v2_stage1_to_f103_cubeide.py` now has an optional command:

```text
python tools/sync_v2_stage1_to_f103_cubeide.py --can-loopback-self-test
```

It temporarily sets F103 bxCAN to internal loopback mode and injects one
well-formed, but intentionally unsupported, 0x601 frame after `CanIf_Start`.
The test is entirely inside the F103 MCU: it excludes PA11/PA12, the CAN
transceiver, bus termination, CANH/CANL wiring, and F407.  A debugger can read
`CanIf_GetTxCount()` and `CanIf_GetRxCount()` after boot.  Both counters must
be non-zero for controller TX, FIFO0 RX interrupt, HAL callback and the CanIf
RX queue path to be considered healthy.

The normal sync command removes the temporary hook and restores
`CAN_MODE_NORMAL`, so no diagnostic-only behavior remains in the final
firmware.

## Step 15: F407-local bxCAN loopback isolation test

The F103 self-test entered its CAN RX callback without an external frame,
confirming its internal controller and ISR path.  To avoid assuming the Tester
is healthy merely because its UART bridge is alive, a matching reversible
F407 diagnostic was added in
`tools/patch_f407_can_loopback_selftest.py`.

```text
python tools/patch_f407_can_loopback_selftest.py --enable
```

The temporary build switches CAN1 to internal loopback and injects one 0x602
frame after the normal tester CAN initialization.  Existing F407 RX filtering
accepts 0x602, so the existing `HAL_CAN_RxFifo0MsgPendingCallback` increments
the runtime CAN ISR counter.  This is checked over UART with the existing
`serial_bridge.py runtime` command.  The command without `--enable` removes
the injected frame and restores normal mode.

## Step 16: Directional physical CAN probe (F103 TX -> F407 RX)

Both controller-local loopback tests passed, but external UDS still did not
reach the F103 RX0 ISR.  Instead of repeatedly changing wiring, the F103 sync
tool adds an optional normal-mode probe:

```text
python tools/sync_v2_stage1_to_f103_cubeide.py --can-external-tx-probe
```

The temporary hook sends one raw 0x602 / `A5` frame every 500 ms from the
heartbeat task.  F407 already accepts 0x602 into its existing CAN RX queue;
`serial_bridge.py can-drain` therefore reports the raw frames if the external
direction is healthy.  This isolates the real physical path
F103 PA12 -> F103 transceiver -> CAN bus -> F407 transceiver -> F407 CAN RX.
It deliberately does not use UDS, CAN-TP or ECU request handling.  A normal
sync removes the hook.

Hardware finding (2026-07-17):

The initial F103-to-F407 probe produced no received frame.  Board-local CAN
loopback had already passed on both MCUs, so supply voltage was checked at the
transceiver modules.  F103 was approximately 3.2 V, but the F407 CAN module
measured only approximately 1 V between its `3.3V` and `GND` header pins.
This is outside the SN65HVD230 operating supply and explains both directions
of the failed external CAN link and intermittent F407 Bus-Off.  The immediate
corrective action is to repair the F407 module's 3.3 V and GND connection,
then rerun the directional probe before returning to UDS regression.

Resolution:

The apparent low module voltage was traced to the CAN/LIN harness ground not
being tied to the F103/F407 system ground.  After adding the common ground,
the physical reference issue was resolved.  The lesson is that a 60-ohm
CANH-to-CANL termination reading alone cannot prove a usable CAN physical
layer: both transceivers also require valid local supply and a shared ground
reference for a robust two-node bench setup.

Final validation after restoring production firmware:

```text
can-drain: queueEmpty=true, no residual A5 probe frames
0x601 22 F1 90 -> 62 F1 90 46 31 30 33 (first poll)
```

The temporary F103 external probe was removed through the normal sync command
before this result.  This validates the final physical-link repair on the
actual UDS request/response path, not only on the diagnostic probe frame.

Regression confirmation:

```text
Full firmware regression: 16/16 PASS
```

The completed matrix covers physical/functional addressing, TesterPresent,
session handling, DID negative paths, DTC trigger/read/clear, and unsupported
service handling after the common-ground repair.

## Step 17: MPU6050 real-provider bring-up

With the GY-521 wired to F103 I2C1 (PB6/PB7, 3.3 V, shared GND, address
0x68), the first read after hot-plug returned `F1C0 = 02 02`: real provider
selected but unhealthy due to I2C communication failure. Electrical checks
showed the sensor module's onboard pull-ups correctly raised SCL and SDA to
3.3 V when disconnected from F103. The F103 I2C1 MSP and `.ioc` settings were
verified as PB6=SCL, PB7=SDA, alternate-function open-drain.

The MPU was then kept connected while F103 was reset. This reinitialized the
I2C state machine against an already-present device and produced:

```text
0x601 22 F1 C0 -> 62 F1 C0 02 01
```

`02 01` proves the real MPU6050 provider is selected and healthy: the startup
`WHO_AM_I=0x68` read, wake/configuration writes and periodic sensor sampling
path are operating on physical hardware. Next acceptance reads validate
motion-dependent F1C1..F1C4 values and increasing F1C5 sample count.

Dynamic DID validation passed on hardware:

```text
Initial: F1C1=+94 mg,  F1C2=-39 mg, F1C3=+1050 mg, F1C4=27.8 C,
         F1C5=1942 samples
After moving the module: F1C1=-476 mg, F1C2=-22 mg, F1C3=+905 mg,
                          F1C5=3381 samples
```

The changing signed acceleration values and increasing 100-ms sample counter
confirm live physical MPU6050 sampling, integer engineering-unit conversion,
SensorApp snapshot ownership, the monitorTask schedule and UDS DID
serialization as one end-to-end path.

Final regression with the real sensor active:

```text
Complete firmware regression: 16/16 PASS
```

This confirms the MPU6050 provider runs concurrently with the production UDS,
DTC, session, CAN-TP and RTOS paths without regressing the existing diagnostic
contract. The real-load implementation and its primary hardware acceptance
are complete. A physical sensor-unplug-to-DTC `C00300` demonstration remains
an optional robustness extension, not a release blocker.

Physical fault/recovery closure was subsequently completed:

```text
MPU VCC disconnected:
  F1C0 -> 62 F1 C0 02 02
  F1C6 -> 62 F1 C6 01
  19 02 FF -> 59 02 09 C0 03 00 09  (C00300)

MPU VCC reconnected and F103 reset:
  F1C0 -> 62 F1 C0 02 01
  14 FF FF FF -> 54
  19 01 FF -> 59 01 09 01 00 00
```

This completes the physical normal -> I2C fault -> DTC report -> recovery ->
explicit DTC clear sequence on the deployed system.
```
