import argparse
import time
import json
import platform
import sys

from minidcm_uart_link import (
    CMD_CAN_RX_POLL,
    CMD_CAN_TX,
    CMD_CAN_TX_BURST,
    CMD_GET_INFO,
    CMD_GET_RUNTIME,
    CMD_PING,
    CMD_TRACE_BAD_CRC,
    CMD_TRACE_STATS,
    build_frame,
    parse_one,
    response_cmd,
)
from cantp_codec import (
    CanTpCodecError,
    CanTpReassembler,
    encode_single_frame,
    frame_type,
    parse_flow_control,
    segment_pdu,
    st_min_to_seconds,
)


# Windows PowerShell may still expose a legacy GBK stdout. Diagnostic payloads
# are binary and their replacement-decoded text can contain characters outside
# GBK, so force JSON transport output to UTF-8.
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="backslashreplace")
except AttributeError:
    pass


def emit(payload, code=0):
    print(json.dumps(payload, ensure_ascii=False))
    return code


def list_ports():
    try:
        from serial.tools import list_ports
    except ModuleNotFoundError:
        return emit(
            {
                "ok": False,
                "platform": platform.system(),
                "ports": [],
                "error": "pyserial is not installed in the selected Python environment.",
                "hint": "Install it with: python -m pip install pyserial",
            },
            code=2,
        )

    ports = []
    for port in list_ports.comports():
        ports.append(
            {
                "path": port.device or "",
                "name": port.name or "",
                "description": port.description or "",
                "manufacturer": port.manufacturer or "",
                "pnpId": port.hwid or "",
                "vid": f"0x{port.vid:04X}" if port.vid is not None else "",
                "pid": f"0x{port.pid:04X}" if port.pid is not None else "",
                "serialNumber": port.serial_number or "",
            }
        )

    return emit(
        {
            "ok": True,
            "platform": platform.system(),
            "ports": ports,
        }
    )


def parse_hex(hex_text):
    compact = "".join(str(hex_text).replace(",", " ").split())
    if len(compact) % 2 != 0:
        raise ValueError("Hex payload must contain an even number of digits.")
    try:
        return bytes.fromhex(compact)
    except ValueError as exc:
        raise ValueError(f"Invalid hex payload: {exc}") from exc


def bytes_to_hex(data):
    return " ".join(f"{byte:02X}" for byte in data)


def parse_can_id(text):
    try:
        can_id = int(str(text), 0)
    except ValueError as exc:
        raise ValueError(f"Invalid CAN ID: {text}") from exc
    if not 0 <= can_id <= 0x7FF:
        raise ValueError("Only 11-bit standard CAN IDs are supported for now: 0x000..0x7FF")
    return can_id


def build_can_tx_payload(can_id, data):
    if len(data) > 8:
        raise ValueError("Classic CAN data length must be 0..8 bytes.")
    return can_id.to_bytes(2, "little") + bytes([len(data)]) + data


def build_cantp_single_frame(uds_payload):
    if not 1 <= len(uds_payload) <= 7:
        raise ValueError("This bring-up helper supports CAN-TP Single Frame payloads of 1..7 bytes only.")
    return bytes([len(uds_payload)]) + uds_payload + bytes(7 - len(uds_payload))


def parse_can_poll_payload(payload):
    if len(payload) < 5:
        return {
            "hasFrame": False,
            "rawPayloadHex": bytes_to_hex(payload),
            "parseWarning": "CAN poll payload is shorter than expected.",
        }

    has_frame = payload[0] != 0
    rx_count = int.from_bytes(payload[1:5], "little")
    result = {
        "hasFrame": has_frame,
        "rxCount": rx_count,
    }
    if not has_frame:
        return result

    if len(payload) < 8:
        result["parseWarning"] = "CAN frame payload is shorter than expected."
        result["rawPayloadHex"] = bytes_to_hex(payload)
        return result

    can_id = int.from_bytes(payload[5:7], "little")
    dlc = payload[7]
    data = payload[8 : 8 + min(dlc, 8)]
    result.update(
        {
            "canId": f"0x{can_id:03X}",
            "dlc": dlc,
            "dataHex": bytes_to_hex(data),
            "dataBytes": list(data),
        }
    )
    return result


def parse_cantp_single_frame(can_data):
    if not can_data:
        raise ValueError("Empty CAN data cannot contain a CAN-TP Single Frame.")
    pci = can_data[0]
    frame_type = (pci >> 4) & 0x0F
    payload_len = pci & 0x0F
    if frame_type != 0:
        raise ValueError(f"Expected CAN-TP Single Frame, got frame type 0x{frame_type:X}.")
    if payload_len > 7 or len(can_data) < 1 + payload_len:
        raise ValueError("Invalid CAN-TP Single Frame payload length.")
    return can_data[1 : 1 + payload_len]


def parse_can_tx_payload(payload):
    result = {
        "canTxAccepted": bool(payload and payload[0] == 0),
    }
    if len(payload) >= 5:
        can_id = int.from_bytes(payload[1:3], "little")
        result.update(
            {
                "canId": f"0x{can_id:03X}",
                "dlc": payload[3],
                "mailbox": f"0x{payload[4]:02X}",
            }
        )
    if len(payload) >= 12:
        error_code = int.from_bytes(payload[7:11], "little")
        result.update(
            {
                "txFreeMailboxes": payload[5],
                "halCanState": f"0x{payload[6]:02X}",
                "halCanError": f"0x{error_code:08X}",
                "canErrorSeen": bool(payload[11]),
            }
        )
    return result


def parse_can_burst_payload(payload):
    if len(payload) < 9:
        return {
            "rawPayloadHex": bytes_to_hex(payload),
            "parseWarning": "CAN burst acknowledgement is shorter than expected.",
        }
    return {
        "canBurstFullyAccepted": payload[0] == 0,
        "canId": f"0x{int.from_bytes(payload[1:3], 'little'):03X}",
        "dlc": payload[3],
        "requestedCount": payload[4],
        "acceptedCount": payload[5],
        "rejectedCount": payload[6],
        "queuePending": payload[7],
        "queueHighWater": payload[8],
    }


def parse_runtime_payload(payload):
    if len(payload) < 17:
        return {
            "rawPayloadHex": bytes_to_hex(payload),
            "parseWarning": "Runtime payload is shorter than 17 bytes.",
        }
    result = {
        "runtimeVersion": payload[0],
        "uartIsrNotifyCount": int.from_bytes(payload[1:5], "little"),
        "canIsrNotifyCount": int.from_bytes(payload[5:9], "little"),
        "bridgeWakeupCount": int.from_bytes(payload[9:13], "little"),
        "heartbeatCount": int.from_bytes(payload[13:17], "little"),
    }
    if len(payload) >= 21:
        result.update({
            "bridgeStackHighWaterWords": int.from_bytes(payload[17:19], "little"),
            "heartbeatStackHighWaterWords": int.from_bytes(payload[19:21], "little"),
        })
    if len(payload) >= 29:
        result.update({
            "canTxQueuePending": payload[21],
            "canTxQueueHighWater": payload[22],
            "canTxQueueRejectCount": int.from_bytes(payload[23:25], "little"),
            "canTxQueueEnqueueCount": int.from_bytes(payload[25:29], "little"),
        })
    if len(payload) >= 37:
        result.update({
            "canRxQueuePending": payload[29],
            "canRxQueueHighWater": payload[30],
            "canRxQueueRejectCount": int.from_bytes(payload[31:33], "little"),
            "canRxQueueEnqueueCount": int.from_bytes(payload[33:37], "little"),
        })
    return result


def parse_trace_stats_payload(payload):
    if len(payload) < 41:
        return {
            "rawPayloadHex": bytes_to_hex(payload),
            "parseWarning": "Trace stats payload is shorter than 41 bytes.",
        }
    names = (
        "framesEnqueued",
        "framesSent",
        "queueRejected",
        "dmaIrqCount",
        "dmaErrorCount",
        "timeoutCount",
        "recoveryCount",
        "backpressureCount",
        "badCrcInjected",
    )
    result = {
        "traceStatsVersion": payload[0],
        "state": payload[1],
        "readyInput": bool(payload[2]),
        "queuePending": payload[3],
        "queueHighWater": payload[4],
    }
    for index, name in enumerate(names):
        offset = 5 + (index * 4)
        result[name] = int.from_bytes(payload[offset : offset + 4], "little")
    return result


def exchange_protocol_frame(ser, seq, request_cmd, payload, timeout_ms):
    tx_data = build_frame(seq=seq, cmd=request_cmd, payload=payload)
    written = ser.write(tx_data)
    ser.flush()
    frame, tail = read_protocol_response(ser, timeout_ms)
    return written, tx_data, frame, tail


def read_protocol_response(ser, timeout_ms):
    deadline = time.monotonic() + timeout_ms / 1000.0
    rx_buffer = bytearray()

    while time.monotonic() < deadline:
        waiting = ser.in_waiting
        if waiting:
            rx_buffer.extend(ser.read(waiting))
            frame, consumed = parse_one(bytes(rx_buffer))
            if consumed:
                del rx_buffer[:consumed]
            if frame is not None:
                return frame, bytes(rx_buffer)
        else:
            time.sleep(0.01)

    return None, bytes(rx_buffer)


def protocol_command(args):
    try:
        import serial
    except ModuleNotFoundError:
        return emit(
            {
                "ok": False,
                "error": "pyserial is not installed in the selected Python environment.",
                "hint": "Install it with: python -m pip install pyserial",
            },
            code=2,
        )

    if args.command == "ping":
        request_cmd = CMD_PING
        payload = (args.payload or "").encode(args.encoding)
    elif args.command == "info":
        request_cmd = CMD_GET_INFO
        payload = b""
    elif args.command == "runtime":
        request_cmd = CMD_GET_RUNTIME
        payload = b""
    elif args.command == "trace-stats":
        request_cmd = CMD_TRACE_STATS
        payload = b""
    elif args.command == "trace-bad-crc":
        request_cmd = CMD_TRACE_BAD_CRC
        payload = b""
    elif args.command == "can-tx":
        request_cmd = CMD_CAN_TX
        try:
            payload = build_can_tx_payload(parse_can_id(args.id), parse_hex(args.hex or ""))
        except ValueError as exc:
            return emit({"ok": False, "error": str(exc)}, code=2)
    elif args.command == "can-burst":
        try:
            can_data = parse_hex(args.hex or "")
            if len(can_data) > 8:
                raise ValueError("Classic CAN data length must be 0..8 bytes.")
            if not 1 <= args.count <= 32:
                raise ValueError("--count must be in the range 1..32.")
            payload = (
                parse_can_id(args.id).to_bytes(2, "little")
                + bytes([len(can_data), args.count])
                + can_data
            )
            request_cmd = CMD_CAN_TX_BURST
        except ValueError as exc:
            return emit({"ok": False, "error": str(exc)}, code=2)
    elif args.command == "can-poll":
        request_cmd = CMD_CAN_RX_POLL
        payload = b""
    else:
        return emit({"ok": False, "error": f"Unsupported protocol command: {args.command}"}, code=2)

    try:
        tx_data = build_frame(seq=args.seq, cmd=request_cmd, payload=payload)
    except ValueError as exc:
        return emit({"ok": False, "error": str(exc)}, code=2)

    try:
        with serial.Serial(
            port=args.port,
            baudrate=args.baudrate,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.05,
            write_timeout=args.timeout_ms / 1000.0,
        ) as ser:
            if args.dtr is not None:
                ser.dtr = args.dtr == "on"
            if args.rts is not None:
                ser.rts = args.rts == "on"

            ser.reset_input_buffer()
            ser.reset_output_buffer()
            written = ser.write(tx_data)
            ser.flush()
            frame, tail = read_protocol_response(ser, args.timeout_ms)

        if frame is None:
            return emit(
                {
                    "ok": False,
                    "port": args.port,
                    "baudrate": args.baudrate,
                    "txBytes": written,
                    "txHex": bytes_to_hex(tx_data),
                    "error": "No valid Mini-DCM UART response frame received before timeout.",
                    "tailHex": bytes_to_hex(tail),
                },
                code=1,
            )

        expected_cmd = response_cmd(request_cmd)
        result = {
            "ok": frame.cmd == expected_cmd and frame.seq == args.seq,
            "port": args.port,
            "baudrate": args.baudrate,
            "seq": frame.seq,
            "requestCmd": f"0x{request_cmd:02X}",
            "responseCmd": f"0x{frame.cmd:02X}",
            "expectedResponseCmd": f"0x{expected_cmd:02X}",
            "txBytes": written,
            "txHex": bytes_to_hex(tx_data),
            "rxPayloadBytes": len(frame.payload),
            "rxPayloadHex": bytes_to_hex(frame.payload),
            "rxPayloadText": frame.payload.decode(args.encoding, errors="replace"),
        }

        if args.command == "can-poll":
            result.update(parse_can_poll_payload(frame.payload))
        elif args.command == "can-tx":
            result.update(parse_can_tx_payload(frame.payload))
        elif args.command == "can-burst":
            result.update(parse_can_burst_payload(frame.payload))
        elif args.command == "runtime":
            result.update(parse_runtime_payload(frame.payload))
        elif args.command == "trace-stats":
            result.update(parse_trace_stats_payload(frame.payload))

        return emit(result)
    except Exception as exc:
        return emit(
            {
                "ok": False,
                "port": args.port,
                "baudrate": args.baudrate,
                "error": str(exc),
            },
            code=1,
        )


def uds_request(args):
    try:
        import serial
    except ModuleNotFoundError:
        return emit(
            {
                "ok": False,
                "error": "pyserial is not installed in the selected Python environment.",
                "hint": "Install it with: python -m pip install pyserial",
            },
            code=2,
        )

    try:
        uds_payload = parse_hex(args.hex)
        sf_data = build_cantp_single_frame(uds_payload)
        can_tx_payload = build_can_tx_payload(parse_can_id(args.id), sf_data)
    except ValueError as exc:
        return emit({"ok": False, "error": str(exc)}, code=2)

    try:
        with serial.Serial(
            port=args.port,
            baudrate=args.baudrate,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.05,
            write_timeout=args.timeout_ms / 1000.0,
        ) as ser:
            if args.dtr is not None:
                ser.dtr = args.dtr == "on"
            if args.rts is not None:
                ser.rts = args.rts == "on"

            ser.reset_input_buffer()
            ser.reset_output_buffer()

            written, tx_data, tx_frame, tx_tail = exchange_protocol_frame(
                ser, args.seq, CMD_CAN_TX, can_tx_payload, args.timeout_ms
            )
            if tx_frame is None:
                return emit(
                    {
                        "ok": False,
                        "port": args.port,
                        "baudrate": args.baudrate,
                        "txBytes": written,
                        "txHex": bytes_to_hex(tx_data),
                        "error": "No CAN_TX acknowledgement from Tester before timeout.",
                        "tailHex": bytes_to_hex(tx_tail),
                    },
                    code=1,
                )

            tx_info = parse_can_tx_payload(tx_frame.payload)
            if tx_frame.cmd != response_cmd(CMD_CAN_TX) or not tx_info.get("canTxAccepted", False):
                return emit(
                    {
                        "ok": False,
                        "port": args.port,
                        "baudrate": args.baudrate,
                        "requestCanId": f"0x{parse_can_id(args.id):03X}",
                        "udsRequestHex": bytes_to_hex(uds_payload),
                        "canTpSingleFrameHex": bytes_to_hex(sf_data),
                        "canTxAck": tx_info,
                        "canTxAckPayloadHex": bytes_to_hex(tx_frame.payload),
                    },
                    code=1,
                )

            deadline = time.monotonic() + args.timeout_ms / 1000.0
            poll_attempts = 0
            last_poll_payload = b""
            ignored_response_frames = []
            expected_positive_sid = (uds_payload[0] + 0x40) & 0xFF
            while time.monotonic() < deadline:
                poll_attempts += 1
                _, _, poll_frame, _ = exchange_protocol_frame(
                    ser, args.seq, CMD_CAN_RX_POLL, b"", min(300, args.timeout_ms)
                )
                if poll_frame is not None and poll_frame.cmd == response_cmd(CMD_CAN_RX_POLL):
                    last_poll_payload = poll_frame.payload
                    poll_info = parse_can_poll_payload(poll_frame.payload)
                    if poll_info.get("hasFrame") and poll_info.get("canId") == f"0x{args.response_id:03X}":
                        can_data = bytes(poll_info.get("dataBytes", []))
                        try:
                            uds_response = parse_cantp_single_frame(can_data)
                        except ValueError:
                            ignored_response_frames.append(poll_info.get("dataHex", ""))
                            continue
                        if (
                            (len(uds_response) == 0)
                            or (
                                (uds_response[0] != expected_positive_sid)
                                and (
                                    (len(uds_response) < 2)
                                    or (uds_response[0] != 0x7F)
                                    or (uds_response[1] != uds_payload[0])
                                )
                            )
                        ):
                            ignored_response_frames.append(poll_info.get("dataHex", ""))
                            continue
                        return emit(
                            {
                                "ok": True,
                                "port": args.port,
                                "baudrate": args.baudrate,
                                "requestCanId": f"0x{parse_can_id(args.id):03X}",
                                "responseCanId": poll_info["canId"],
                                "udsRequestHex": bytes_to_hex(uds_payload),
                                "canTpRequestHex": bytes_to_hex(sf_data),
                                "canResponseDataHex": poll_info["dataHex"],
                                "udsResponseHex": bytes_to_hex(uds_response),
                                "udsResponseText": uds_response.decode(args.encoding, errors="replace"),
                                "pollAttempts": poll_attempts,
                                "ignoredResponseFrames": ignored_response_frames,
                                "canTxAck": tx_info,
                            }
                        )
                time.sleep(args.poll_interval_ms / 1000.0)

        return emit(
            {
                "ok": False,
                "port": args.port,
                "baudrate": args.baudrate,
                "requestCanId": f"0x{parse_can_id(args.id):03X}",
                "expectedResponseCanId": f"0x{args.response_id:03X}",
                "udsRequestHex": bytes_to_hex(uds_payload),
                "canTpRequestHex": bytes_to_hex(sf_data),
                "error": "No CAN-TP Single Frame UDS response received before timeout.",
                "pollAttempts": poll_attempts,
                "lastPollPayloadHex": bytes_to_hex(last_poll_payload),
                "ignoredResponseFrames": ignored_response_frames,
                "canTxAck": tx_info,
            },
            code=1,
        )
    except Exception as exc:
        return emit(
            {
                "ok": False,
                "port": args.port,
                "baudrate": args.baudrate,
                "error": str(exc),
            },
            code=1,
        )


def uds_multi_request(args):
    """Send an SF UDS request and reassemble an FF/FC/CF UDS response.

    V2.9-2 intentionally limits requests to Single Frame.  It validates the
    ECU multi-frame response path before adding multi-frame requests.
    """
    try:
        import serial
    except ModuleNotFoundError:
        return emit({"ok": False, "error": "pyserial is not installed in the selected Python environment."}, code=2)

    try:
        uds_payload = parse_hex(args.hex)
        request_can_id = parse_can_id(args.id)
        request_data = encode_single_frame(uds_payload)
    except (ValueError, CanTpCodecError) as exc:
        return emit({"ok": False, "error": str(exc)}, code=2)

    try:
        with serial.Serial(
            port=args.port,
            baudrate=args.baudrate,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.05,
            write_timeout=args.timeout_ms / 1000.0,
        ) as ser:
            if args.dtr is not None:
                ser.dtr = args.dtr == "on"
            if args.rts is not None:
                ser.rts = args.rts == "on"

            ser.reset_input_buffer()
            ser.reset_output_buffer()
            _, _, tx_frame, tx_tail = exchange_protocol_frame(
                ser, args.seq, CMD_CAN_TX, build_can_tx_payload(request_can_id, request_data), args.timeout_ms
            )
            if tx_frame is None:
                return emit(
                    {
                        "ok": False,
                        "port": args.port,
                        "udsRequestHex": bytes_to_hex(uds_payload),
                        "error": "No CAN_TX acknowledgement before timeout.",
                        "tailHex": bytes_to_hex(tx_tail),
                    },
                    code=1,
                )

            tx_info = parse_can_tx_payload(tx_frame.payload)
            if tx_frame.cmd != response_cmd(CMD_CAN_TX) or not tx_info.get("canTxAccepted", False):
                return emit(
                    {
                        "ok": False,
                        "port": args.port,
                        "udsRequestHex": bytes_to_hex(uds_payload),
                        "canTpRequestHex": bytes_to_hex(request_data),
                        "canTxAck": tx_info,
                        "error": "Tester did not accept the CAN-TP request frame.",
                    },
                    code=1,
                )

            receiver = CanTpReassembler(block_size=args.fc_block_size, st_min=args.fc_stmin)
            deadline = time.monotonic() + args.timeout_ms / 1000.0
            poll_attempts = 0
            response_frames = []
            response_frame_times = []
            fc_sent = False
            flow_control_count = 0

            while time.monotonic() < deadline:
                poll_attempts += 1
                _, _, poll_frame, _ = exchange_protocol_frame(
                    ser, args.seq, CMD_CAN_RX_POLL, b"", min(300, args.timeout_ms)
                )
                if poll_frame is None or poll_frame.cmd != response_cmd(CMD_CAN_RX_POLL):
                    continue

                poll_info = parse_can_poll_payload(poll_frame.payload)
                if not poll_info.get("hasFrame") or poll_info.get("canId") != f"0x{args.response_id:03X}":
                    time.sleep(args.poll_interval_ms / 1000.0)
                    continue

                can_data = bytes(poll_info.get("dataBytes", []))
                response_frames.append(poll_info.get("dataHex", ""))
                response_frame_times.append(time.monotonic())
                uds_response, fc_frame = receiver.feed(can_data)
                if fc_frame is not None:
                    _, _, fc_ack_frame, _ = exchange_protocol_frame(
                        ser,
                        args.seq,
                        CMD_CAN_TX,
                        build_can_tx_payload(request_can_id, fc_frame),
                        args.timeout_ms,
                    )
                    if fc_ack_frame is None:
                        return emit(
                            {
                                "ok": False,
                                "port": args.port,
                                "udsRequestHex": bytes_to_hex(uds_payload),
                                "error": "ECU First Frame received, but Tester did not acknowledge Flow Control transmission.",
                                "canResponseFrames": response_frames,
                            },
                            code=1,
                        )
                    fc_ack = parse_can_tx_payload(fc_ack_frame.payload)
                    if not fc_ack.get("canTxAccepted", False):
                        return emit(
                            {
                                "ok": False,
                                "port": args.port,
                                "udsRequestHex": bytes_to_hex(uds_payload),
                                "error": "Tester rejected Flow Control transmission.",
                                "flowControlHex": bytes_to_hex(fc_frame),
                                "canTxAck": fc_ack,
                            },
                            code=1,
                        )
                    fc_sent = True
                    flow_control_count += 1

                if uds_response is not None:
                    return emit(
                        {
                            "ok": True,
                            "port": args.port,
                            "baudrate": args.baudrate,
                            "requestCanId": f"0x{request_can_id:03X}",
                            "responseCanId": f"0x{args.response_id:03X}",
                            "udsRequestHex": bytes_to_hex(uds_payload),
                            "canTpRequestHex": bytes_to_hex(request_data),
                            "canResponseFrames": response_frames,
                            "canResponseFrameIntervalsMs": [
                                round((response_frame_times[index] - response_frame_times[index - 1]) * 1000, 3)
                                for index in range(1, len(response_frame_times))
                            ],
                            "flowControlSent": fc_sent,
                            "flowControlCount": flow_control_count,
                            "requestedFlowControlBlockSize": args.fc_block_size,
                            "udsResponseHex": bytes_to_hex(uds_response),
                            "udsResponseText": uds_response.decode(args.encoding, errors="replace"),
                            "pollAttempts": poll_attempts,
                            "canTxAck": tx_info,
                        }
                    )
                time.sleep(args.poll_interval_ms / 1000.0)

        return emit(
            {
                "ok": False,
                "port": args.port,
                "baudrate": args.baudrate,
                "udsRequestHex": bytes_to_hex(uds_payload),
                "error": "No complete CAN-TP multi-frame UDS response received before timeout.",
                "pollAttempts": poll_attempts,
                "canResponseFrames": response_frames,
                "canResponseFrameIntervalsMs": [
                    round((response_frame_times[index] - response_frame_times[index - 1]) * 1000, 3)
                    for index in range(1, len(response_frame_times))
                ],
                "flowControlSent": fc_sent,
                "flowControlCount": flow_control_count,
                "canTxAck": tx_info,
            },
            code=1,
        )
    except (CanTpCodecError, ValueError) as exc:
        return emit({"ok": False, "port": args.port, "baudrate": args.baudrate, "error": str(exc)}, code=1)
    except Exception as exc:
        return emit({"ok": False, "port": args.port, "baudrate": args.baudrate, "error": str(exc)}, code=1)


def uds_multi_tx_request(args):
    """Send an FF/CF UDS request, honour ECU FC, then reassemble its response."""
    try:
        import serial
    except ModuleNotFoundError:
        return emit({"ok": False, "error": "pyserial is not installed in the selected Python environment."}, code=2)

    try:
        uds_payload = parse_hex(args.hex)
        request_can_id = parse_can_id(args.id)
        request_frames = segment_pdu(uds_payload)
        if len(request_frames) < 2:
            raise CanTpCodecError("uds-multi-tx requires a UDS PDU longer than 7 bytes.")
    except (ValueError, CanTpCodecError) as exc:
        return emit({"ok": False, "error": str(exc)}, code=2)

    try:
        with serial.Serial(
            port=args.port,
            baudrate=args.baudrate,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.05,
            write_timeout=args.timeout_ms / 1000.0,
        ) as ser:
            if args.dtr is not None:
                ser.dtr = args.dtr == "on"
            if args.rts is not None:
                ser.rts = args.rts == "on"
            ser.reset_input_buffer()
            ser.reset_output_buffer()

            def send_can_frame(can_data):
                _, _, ack_frame, _ = exchange_protocol_frame(
                    ser,
                    args.seq,
                    CMD_CAN_TX,
                    build_can_tx_payload(request_can_id, can_data),
                    args.timeout_ms,
                )
                if ack_frame is None or ack_frame.cmd != response_cmd(CMD_CAN_TX):
                    raise CanTpCodecError("Tester did not acknowledge CAN frame transmission.")
                ack = parse_can_tx_payload(ack_frame.payload)
                if not ack.get("canTxAccepted", False):
                    raise CanTpCodecError(f"Tester rejected CAN frame transmission: {ack}")
                return ack

            first_ack = send_can_frame(request_frames[0])
            deadline = time.monotonic() + args.timeout_ms / 1000.0
            poll_attempts = 0
            ecu_fc = None
            while time.monotonic() < deadline:
                poll_attempts += 1
                _, _, poll_frame, _ = exchange_protocol_frame(
                    ser, args.seq, CMD_CAN_RX_POLL, b"", min(300, args.timeout_ms)
                )
                if poll_frame is None or poll_frame.cmd != response_cmd(CMD_CAN_RX_POLL):
                    continue
                info = parse_can_poll_payload(poll_frame.payload)
                if not info.get("hasFrame") or info.get("canId") != f"0x{args.response_id:03X}":
                    continue
                data = bytes(info.get("dataBytes", []))
                if frame_type(data) == 0x03:
                    flow_status, block_size, st_min = parse_flow_control(data)
                    if flow_status != 0:
                        raise CanTpCodecError(f"ECU Flow Control status is not CTS: {flow_status}.")
                    ecu_fc = {"dataHex": info.get("dataHex"), "blockSize": block_size, "stMin": st_min}
                    break

            if ecu_fc is None:
                return emit(
                    {
                        "ok": False,
                        "port": args.port,
                        "udsRequestHex": bytes_to_hex(uds_payload),
                        "canTpRequestFrames": [bytes_to_hex(frame) for frame in request_frames],
                        "error": "No ECU Flow Control CTS received after First Frame.",
                        "pollAttempts": poll_attempts,
                    },
                    code=1,
                )

            ecu_flow_controls = [ecu_fc]
            current_block_size = ecu_fc["blockSize"]
            current_st_min_seconds = st_min_to_seconds(ecu_fc["stMin"])
            cf_in_block = 0
            last_cf_sent_at = None
            applied_st_min_ms = []
            for cf_index, cf_frame in enumerate(request_frames[1:], start=1):
                if last_cf_sent_at is not None and current_st_min_seconds > 0:
                    remaining_delay = current_st_min_seconds - (time.monotonic() - last_cf_sent_at)
                    if remaining_delay > 0:
                        time.sleep(remaining_delay)
                send_can_frame(cf_frame)
                last_cf_sent_at = time.monotonic()
                applied_st_min_ms.append(round(current_st_min_seconds * 1000, 3))
                cf_in_block += 1
                if ((current_block_size != 0) and
                    (cf_in_block >= current_block_size) and
                    (cf_index < (len(request_frames) - 1))):
                    deadline = time.monotonic() + args.timeout_ms / 1000.0
                    next_fc = None
                    while time.monotonic() < deadline:
                        poll_attempts += 1
                        _, _, poll_frame, _ = exchange_protocol_frame(
                            ser, args.seq, CMD_CAN_RX_POLL, b"", min(300, args.timeout_ms)
                        )
                        if poll_frame is None or poll_frame.cmd != response_cmd(CMD_CAN_RX_POLL):
                            continue
                        info = parse_can_poll_payload(poll_frame.payload)
                        if not info.get("hasFrame") or info.get("canId") != f"0x{args.response_id:03X}":
                            continue
                        data = bytes(info.get("dataBytes", []))
                        if frame_type(data) != 0x03:
                            continue
                        flow_status, block_size, st_min = parse_flow_control(data)
                        if flow_status != 0:
                            raise CanTpCodecError(f"ECU Flow Control status is not CTS: {flow_status}.")
                        next_fc = {"dataHex": info.get("dataHex"), "blockSize": block_size, "stMin": st_min}
                        break
                    if next_fc is None:
                        raise CanTpCodecError("No repeated ECU Flow Control CTS received at Block Size boundary.")
                    ecu_flow_controls.append(next_fc)
                    current_block_size = next_fc["blockSize"]
                    current_st_min_seconds = st_min_to_seconds(next_fc["stMin"])
                    cf_in_block = 0

            receiver = CanTpReassembler()
            response_frames = []
            flow_control_sent = False
            deadline = time.monotonic() + args.timeout_ms / 1000.0
            while time.monotonic() < deadline:
                poll_attempts += 1
                _, _, poll_frame, _ = exchange_protocol_frame(
                    ser, args.seq, CMD_CAN_RX_POLL, b"", min(300, args.timeout_ms)
                )
                if poll_frame is None or poll_frame.cmd != response_cmd(CMD_CAN_RX_POLL):
                    continue
                info = parse_can_poll_payload(poll_frame.payload)
                if not info.get("hasFrame") or info.get("canId") != f"0x{args.response_id:03X}":
                    continue
                data = bytes(info.get("dataBytes", []))
                response_frames.append(info.get("dataHex", ""))
                uds_response, fc_frame = receiver.feed(data)
                if fc_frame is not None:
                    send_can_frame(fc_frame)
                    flow_control_sent = True
                if uds_response is not None:
                    return emit(
                        {
                            "ok": True,
                            "port": args.port,
                            "baudrate": args.baudrate,
                            "requestCanId": f"0x{request_can_id:03X}",
                            "responseCanId": f"0x{args.response_id:03X}",
                            "udsRequestHex": bytes_to_hex(uds_payload),
                            "canTpRequestFrames": [bytes_to_hex(frame) for frame in request_frames],
                            "ecuFlowControl": ecu_fc,
                            "ecuFlowControls": ecu_flow_controls,
                            "appliedStMinMsPerCf": applied_st_min_ms,
                            "canResponseFrames": response_frames,
                            "flowControlSentForResponse": flow_control_sent,
                            "udsResponseHex": bytes_to_hex(uds_response),
                            "udsResponseText": uds_response.decode(args.encoding, errors="replace"),
                            "pollAttempts": poll_attempts,
                            "canTxAck": first_ack,
                        }
                    )
                time.sleep(args.poll_interval_ms / 1000.0)

        return emit(
            {
                "ok": False,
                "port": args.port,
                "udsRequestHex": bytes_to_hex(uds_payload),
                "ecuFlowControl": ecu_fc,
                "error": "No complete UDS response received after CAN-TP multi-frame request.",
                "pollAttempts": poll_attempts,
                "canResponseFrames": response_frames,
            },
            code=1,
        )
    except (CanTpCodecError, ValueError) as exc:
        return emit({"ok": False, "port": args.port, "baudrate": args.baudrate, "error": str(exc)}, code=1)
    except Exception as exc:
        return emit({"ok": False, "port": args.port, "baudrate": args.baudrate, "error": str(exc)}, code=1)


def uds_multi_negative_request(args):
    """Inject one controlled CAN-TP RX negative path into the ECU.

    The command intentionally stops before a complete PDU exists. It never
    claims a UDS positive response; the caller verifies F193 and F18C after it.
    """
    try:
        import serial
    except ModuleNotFoundError:
        return emit({"ok": False, "error": "pyserial is not installed in the selected Python environment."}, code=2)

    try:
        uds_payload = parse_hex(args.hex)
        request_can_id = parse_can_id(args.id)
        request_frames = segment_pdu(uds_payload)
        if len(request_frames) < 2:
            raise CanTpCodecError("uds-multi-negative requires a UDS PDU longer than 7 bytes.")
        if args.mode == "timeout" and args.wait_ms < 1000:
            raise ValueError("--wait-ms must be at least 1000 for the ECU N_Cr timeout test.")
    except (ValueError, CanTpCodecError) as exc:
        return emit({"ok": False, "error": str(exc)}, code=2)

    try:
        with serial.Serial(
            port=args.port,
            baudrate=args.baudrate,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.05,
            write_timeout=args.timeout_ms / 1000.0,
        ) as ser:
            if args.dtr is not None:
                ser.dtr = args.dtr == "on"
            if args.rts is not None:
                ser.rts = args.rts == "on"
            ser.reset_input_buffer()
            ser.reset_output_buffer()

            def send_can_frame(can_data):
                _, _, ack_frame, _ = exchange_protocol_frame(
                    ser,
                    args.seq,
                    CMD_CAN_TX,
                    build_can_tx_payload(request_can_id, can_data),
                    args.timeout_ms,
                )
                if ack_frame is None or ack_frame.cmd != response_cmd(CMD_CAN_TX):
                    raise CanTpCodecError("Tester did not acknowledge CAN frame transmission.")
                ack = parse_can_tx_payload(ack_frame.payload)
                if not ack.get("canTxAccepted", False):
                    raise CanTpCodecError(f"Tester rejected CAN frame transmission: {ack}")
                return ack

            first_ack = send_can_frame(request_frames[0])
            deadline = time.monotonic() + args.timeout_ms / 1000.0
            poll_attempts = 0
            ecu_fc = None
            while time.monotonic() < deadline:
                poll_attempts += 1
                _, _, poll_frame, _ = exchange_protocol_frame(
                    ser, args.seq, CMD_CAN_RX_POLL, b"", min(300, args.timeout_ms)
                )
                if poll_frame is None or poll_frame.cmd != response_cmd(CMD_CAN_RX_POLL):
                    continue
                info = parse_can_poll_payload(poll_frame.payload)
                if not info.get("hasFrame") or info.get("canId") != f"0x{args.response_id:03X}":
                    continue
                data = bytes(info.get("dataBytes", []))
                if frame_type(data) == 0x03:
                    flow_status, block_size, st_min = parse_flow_control(data)
                    if flow_status != 0:
                        raise CanTpCodecError(f"ECU Flow Control status is not CTS: {flow_status}.")
                    ecu_fc = {"dataHex": info.get("dataHex"), "blockSize": block_size, "stMin": st_min}
                    break

            if ecu_fc is None:
                return emit(
                    {
                        "ok": False,
                        "port": args.port,
                        "mode": args.mode,
                        "error": "No ECU Flow Control CTS received after First Frame.",
                        "pollAttempts": poll_attempts,
                    },
                    code=1,
                )

            injected_frame_hex = ""
            if args.mode == "bad-sn":
                malformed_cf = bytearray(request_frames[1])
                malformed_cf[0] = 0x22  # ECU expects SN=1 immediately after the FF.
                send_can_frame(bytes(malformed_cf))
                injected_frame_hex = bytes_to_hex(malformed_cf)
            else:
                time.sleep(args.wait_ms / 1000.0)

        return emit(
            {
                "ok": True,
                "port": args.port,
                "baudrate": args.baudrate,
                "mode": args.mode,
                "udsRequestHex": bytes_to_hex(uds_payload),
                "firstFrameHex": bytes_to_hex(request_frames[0]),
                "ecuFlowControl": ecu_fc,
                "injectedFrameHex": injected_frame_hex,
                "waitMs": args.wait_ms if args.mode == "timeout" else 0,
                "message": "Negative CAN-TP condition injected; read F193 and F18C to verify no write and error reporting.",
                "canTxAck": first_ack,
            }
        )
    except (CanTpCodecError, ValueError) as exc:
        return emit({"ok": False, "port": args.port, "baudrate": args.baudrate, "error": str(exc)}, code=1)
    except Exception as exc:
        return emit({"ok": False, "port": args.port, "baudrate": args.baudrate, "error": str(exc)}, code=1)


def uds_stress(args):
    """Run sequential UDS-over-CAN single-frame round trips on one UART session."""
    try:
        import serial
    except ModuleNotFoundError:
        return emit(
            {
                "ok": False,
                "error": "pyserial is not installed in the selected Python environment.",
            },
            code=2,
        )

    try:
        uds_payload = parse_hex(args.hex)
        expected_response = parse_hex(args.expected)
        sf_data = build_cantp_single_frame(uds_payload)
        request_can_id = parse_can_id(args.id)
        can_tx_payload = build_can_tx_payload(request_can_id, sf_data)
    except ValueError as exc:
        return emit({"ok": False, "error": str(exc)}, code=2)

    failures = []
    latencies_ms = []
    last_response_hex = ""
    start_time = time.monotonic()

    try:
        with serial.Serial(
            port=args.port,
            baudrate=args.baudrate,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.05,
            write_timeout=args.timeout_ms / 1000.0,
        ) as ser:
            if args.dtr is not None:
                ser.dtr = args.dtr == "on"
            if args.rts is not None:
                ser.rts = args.rts == "on"

            ser.reset_input_buffer()
            ser.reset_output_buffer()

            for iteration in range(1, args.count + 1):
                iteration_start = time.monotonic()
                _, _, tx_frame, tx_tail = exchange_protocol_frame(
                    ser, args.seq, CMD_CAN_TX, can_tx_payload, args.timeout_ms
                )
                if tx_frame is None:
                    failures.append({
                        "iteration": iteration,
                        "stage": "can_tx_ack",
                        "error": "No CAN_TX acknowledgement from Tester before timeout.",
                        "tailHex": bytes_to_hex(tx_tail),
                    })
                else:
                    tx_info = parse_can_tx_payload(tx_frame.payload)
                    if tx_frame.cmd != response_cmd(CMD_CAN_TX) or not tx_info.get("canTxAccepted", False):
                        failures.append({
                            "iteration": iteration,
                            "stage": "can_tx_ack",
                            "error": "Tester rejected the CAN transmit request.",
                            "canTxAck": tx_info,
                        })
                    else:
                        deadline = time.monotonic() + args.timeout_ms / 1000.0
                        response_found = False
                        last_poll_payload = b""
                        while time.monotonic() < deadline:
                            _, _, poll_frame, _ = exchange_protocol_frame(
                                ser, args.seq, CMD_CAN_RX_POLL, b"", min(300, args.timeout_ms)
                            )
                            if poll_frame is not None and poll_frame.cmd == response_cmd(CMD_CAN_RX_POLL):
                                last_poll_payload = poll_frame.payload
                                poll_info = parse_can_poll_payload(poll_frame.payload)
                                if poll_info.get("hasFrame") and poll_info.get("canId") == f"0x{args.response_id:03X}":
                                    try:
                                        uds_response = parse_cantp_single_frame(bytes(poll_info.get("dataBytes", [])))
                                        last_response_hex = bytes_to_hex(uds_response)
                                        response_found = True
                                        if uds_response != expected_response:
                                            failures.append({
                                                "iteration": iteration,
                                                "stage": "uds_response",
                                                "error": "UDS response does not match the expected bytes.",
                                                "actualUdsResponseHex": last_response_hex,
                                            })
                                        else:
                                            latencies_ms.append(round((time.monotonic() - iteration_start) * 1000.0, 2))
                                    except ValueError as exc:
                                        failures.append({
                                            "iteration": iteration,
                                            "stage": "can_tp",
                                            "error": str(exc),
                                        })
                                    break
                            time.sleep(args.poll_interval_ms / 1000.0)

                        if not response_found:
                            failures.append({
                                "iteration": iteration,
                                "stage": "can_rx_poll",
                                "error": "No CAN-TP Single Frame UDS response received before timeout.",
                                "lastPollPayloadHex": bytes_to_hex(last_poll_payload),
                            })

                if args.inter_request_ms:
                    time.sleep(args.inter_request_ms / 1000.0)

        passed_count = args.count - len(failures)
        elapsed_ms = round((time.monotonic() - start_time) * 1000.0, 2)
        result = {
            "ok": not failures,
            "port": args.port,
            "baudrate": args.baudrate,
            "requestCanId": f"0x{request_can_id:03X}",
            "responseCanId": f"0x{args.response_id:03X}",
            "udsRequestHex": bytes_to_hex(uds_payload),
            "expectedUdsResponseHex": bytes_to_hex(expected_response),
            "count": args.count,
            "passedCount": passed_count,
            "failedCount": len(failures),
            "elapsedMs": elapsed_ms,
            "lastResponseHex": last_response_hex,
            "failures": failures,
        }
        if latencies_ms:
            result["latencyMs"] = {
                "min": min(latencies_ms),
                "max": max(latencies_ms),
                "average": round(sum(latencies_ms) / len(latencies_ms), 2),
            }
        return emit(result, code=0 if result["ok"] else 1)
    except Exception as exc:
        return emit(
            {
                "ok": False,
                "port": args.port,
                "baudrate": args.baudrate,
                "error": str(exc),
            },
            code=1,
        )


def can_drain(args):
    """Drain buffered F407 CAN RX frames through repeated CAN_RX_POLL commands."""
    try:
        import serial
    except ModuleNotFoundError:
        return emit({"ok": False, "error": "pyserial is not installed in the selected Python environment."}, code=2)

    frames = []
    try:
        with serial.Serial(
            port=args.port,
            baudrate=args.baudrate,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.05,
            write_timeout=args.timeout_ms / 1000.0,
        ) as ser:
            if args.dtr is not None:
                ser.dtr = args.dtr == "on"
            if args.rts is not None:
                ser.rts = args.rts == "on"
            ser.reset_input_buffer()
            ser.reset_output_buffer()

            for _ in range(args.max_frames):
                _, _, frame, tail = exchange_protocol_frame(
                    ser, args.seq, CMD_CAN_RX_POLL, b"", args.timeout_ms
                )
                if frame is None:
                    return emit({
                        "ok": False,
                        "port": args.port,
                        "baudrate": args.baudrate,
                        "error": "No CAN_RX_POLL response from Tester before timeout.",
                        "tailHex": bytes_to_hex(tail),
                        "drainedCount": len(frames),
                    }, code=1)
                if frame.cmd != response_cmd(CMD_CAN_RX_POLL):
                    return emit({
                        "ok": False,
                        "port": args.port,
                        "baudrate": args.baudrate,
                        "error": "Tester returned an unexpected response command while draining CAN RX frames.",
                        "drainedCount": len(frames),
                    }, code=1)

                info = parse_can_poll_payload(frame.payload)
                if not info.get("hasFrame"):
                    return emit({
                        "ok": True,
                        "port": args.port,
                        "baudrate": args.baudrate,
                        "drainedCount": len(frames),
                        "frames": frames,
                        "queueEmpty": True,
                    })
                frames.append({
                    "canId": info.get("canId"),
                    "dlc": info.get("dlc"),
                    "dataHex": info.get("dataHex"),
                    "rxCount": info.get("rxCount"),
                })

        return emit({
            "ok": True,
            "port": args.port,
            "baudrate": args.baudrate,
            "drainedCount": len(frames),
            "frames": frames,
            "queueEmpty": False,
            "warning": "Reached --max-frames before observing an empty queue.",
        })
    except Exception as exc:
        return emit({"ok": False, "port": args.port, "baudrate": args.baudrate, "error": str(exc)}, code=1)


def listen(args):
    try:
        import serial
    except ModuleNotFoundError:
        return emit(
            {
                "ok": False,
                "error": "pyserial is not installed in the selected Python environment.",
                "hint": "Install it with: python -m pip install pyserial",
            },
            code=2,
        )

    try:
        with serial.Serial(
            port=args.port,
            baudrate=args.baudrate,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.05,
        ) as ser:
            if args.dtr is not None:
                ser.dtr = args.dtr == "on"
            if args.rts is not None:
                ser.rts = args.rts == "on"

            ser.reset_input_buffer()
            deadline = time.monotonic() + args.duration_ms / 1000.0
            rx_chunks = []
            while time.monotonic() < deadline:
                waiting = ser.in_waiting
                if waiting:
                    rx_chunks.append(ser.read(waiting))
                else:
                    time.sleep(0.01)

            rx_data = b"".join(rx_chunks)

        return emit(
            {
                "ok": True,
                "port": args.port,
                "baudrate": args.baudrate,
                "durationMs": args.duration_ms,
                "rxBytes": len(rx_data),
                "rxHex": bytes_to_hex(rx_data),
                "rxText": rx_data.decode(args.encoding, errors="replace"),
            }
        )
    except Exception as exc:
        return emit(
            {
                "ok": False,
                "port": args.port,
                "baudrate": args.baudrate,
                "error": str(exc),
            },
            code=1,
        )


def transact(args):
    try:
        import serial
    except ModuleNotFoundError:
        return emit(
            {
                "ok": False,
                "error": "pyserial is not installed in the selected Python environment.",
                "hint": "Install it with: python -m pip install pyserial",
            },
            code=2,
        )

    try:
        if args.hex:
            tx_data = parse_hex(args.hex)
            tx_mode = "hex"
        else:
            tx_data = str(args.text or "").encode(args.encoding)
            tx_mode = "text"
    except ValueError as exc:
        return emit({"ok": False, "error": str(exc)}, code=2)

    try:
        with serial.Serial(
            port=args.port,
            baudrate=args.baudrate,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=args.timeout_ms / 1000.0,
            write_timeout=args.timeout_ms / 1000.0,
        ) as ser:
            if args.dtr is not None:
                ser.dtr = args.dtr == "on"
            if args.rts is not None:
                ser.rts = args.rts == "on"

            ser.reset_input_buffer()
            ser.reset_output_buffer()

            written = ser.write(tx_data)
            ser.flush()

            deadline = time.monotonic() + args.timeout_ms / 1000.0
            rx_chunks = []
            while time.monotonic() < deadline:
                waiting = ser.in_waiting
                if waiting:
                    rx_chunks.append(ser.read(waiting))
                    if args.expected_bytes and sum(len(chunk) for chunk in rx_chunks) >= args.expected_bytes:
                        break
                else:
                    time.sleep(0.01)

            rx_data = b"".join(rx_chunks)

        return emit(
            {
                "ok": True,
                "port": args.port,
                "baudrate": args.baudrate,
                "txMode": tx_mode,
                "txBytes": written,
                "txHex": bytes_to_hex(tx_data),
                "rxBytes": len(rx_data),
                "rxHex": bytes_to_hex(rx_data),
                "rxText": rx_data.decode(args.encoding, errors="replace"),
            }
        )
    except Exception as exc:
        return emit(
            {
                "ok": False,
                "port": args.port,
                "baudrate": args.baudrate,
                "error": str(exc),
            },
            code=1,
        )


def main():
    parser = argparse.ArgumentParser(description="Mini-DCM serial bridge helper")
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("list", help="List available serial ports")
    listen_parser = subparsers.add_parser("listen", help="Listen for bytes without sending")
    listen_parser.add_argument("--port", required=True, help="Serial port, for example COM9")
    listen_parser.add_argument("--baudrate", type=int, default=9600)
    listen_parser.add_argument("--encoding", default="utf-8")
    listen_parser.add_argument("--duration-ms", type=int, default=3000)
    listen_parser.add_argument("--dtr", choices=["on", "off"], default=None)
    listen_parser.add_argument("--rts", choices=["on", "off"], default=None)
    transact_parser = subparsers.add_parser("transact", help="Send bytes and read a short response")
    transact_parser.add_argument("--port", required=True, help="Serial port, for example COM9")
    transact_parser.add_argument("--baudrate", type=int, default=115200)
    payload_group = transact_parser.add_mutually_exclusive_group(required=True)
    payload_group.add_argument("--text", help="Text payload to send")
    payload_group.add_argument("--hex", help="Hex payload to send, for example: 41 42 43")
    transact_parser.add_argument("--encoding", default="utf-8")
    transact_parser.add_argument("--timeout-ms", type=int, default=300)
    transact_parser.add_argument("--expected-bytes", type=int, default=0)
    transact_parser.add_argument("--dtr", choices=["on", "off"], default=None)
    transact_parser.add_argument("--rts", choices=["on", "off"], default=None)
    ping_parser = subparsers.add_parser("ping", help="Send a Mini-DCM UART PING frame")
    ping_parser.add_argument("--port", required=True, help="Serial port, for example COM9")
    ping_parser.add_argument("--baudrate", type=int, default=9600)
    ping_parser.add_argument("--seq", type=int, default=1)
    ping_parser.add_argument("--payload", default="ping")
    ping_parser.add_argument("--encoding", default="utf-8")
    ping_parser.add_argument("--timeout-ms", type=int, default=1000)
    ping_parser.add_argument("--dtr", choices=["on", "off"], default=None)
    ping_parser.add_argument("--rts", choices=["on", "off"], default=None)
    info_parser = subparsers.add_parser("info", help="Send a Mini-DCM UART GET_INFO frame")
    info_parser.add_argument("--port", required=True, help="Serial port, for example COM9")
    info_parser.add_argument("--baudrate", type=int, default=9600)
    info_parser.add_argument("--seq", type=int, default=1)
    info_parser.add_argument("--encoding", default="utf-8")
    info_parser.add_argument("--timeout-ms", type=int, default=1000)
    info_parser.add_argument("--dtr", choices=["on", "off"], default=None)
    info_parser.add_argument("--rts", choices=["on", "off"], default=None)
    runtime_parser = subparsers.add_parser("runtime", help="Read F407 Tester FreeRTOS runtime counters")
    runtime_parser.add_argument("--port", required=True, help="Serial port, for example COM17")
    runtime_parser.add_argument("--baudrate", type=int, default=9600)
    runtime_parser.add_argument("--seq", type=int, default=1)
    runtime_parser.add_argument("--encoding", default="utf-8")
    runtime_parser.add_argument("--timeout-ms", type=int, default=1000)
    runtime_parser.add_argument("--dtr", choices=["on", "off"], default=None)
    runtime_parser.add_argument("--rts", choices=["on", "off"], default=None)
    trace_stats_parser = subparsers.add_parser(
        "trace-stats", help="Read F407 Tester SPI TraceLink counters"
    )
    trace_stats_parser.add_argument("--port", required=True)
    trace_stats_parser.add_argument("--baudrate", type=int, default=9600)
    trace_stats_parser.add_argument("--seq", type=int, default=1)
    trace_stats_parser.add_argument("--encoding", default="utf-8")
    trace_stats_parser.add_argument("--timeout-ms", type=int, default=1000)
    trace_stats_parser.add_argument("--dtr", choices=["on", "off"], default=None)
    trace_stats_parser.add_argument("--rts", choices=["on", "off"], default=None)
    trace_bad_crc_parser = subparsers.add_parser(
        "trace-bad-crc", help="Corrupt the CRC of the next emitted TraceFrame"
    )
    trace_bad_crc_parser.add_argument("--port", required=True)
    trace_bad_crc_parser.add_argument("--baudrate", type=int, default=9600)
    trace_bad_crc_parser.add_argument("--seq", type=int, default=1)
    trace_bad_crc_parser.add_argument("--encoding", default="utf-8")
    trace_bad_crc_parser.add_argument("--timeout-ms", type=int, default=1000)
    trace_bad_crc_parser.add_argument("--dtr", choices=["on", "off"], default=None)
    trace_bad_crc_parser.add_argument("--rts", choices=["on", "off"], default=None)
    can_tx_parser = subparsers.add_parser("can-tx", help="Send one classic CAN data frame through the Tester node")
    can_tx_parser.add_argument("--port", required=True, help="Serial port, for example COM11")
    can_tx_parser.add_argument("--baudrate", type=int, default=9600)
    can_tx_parser.add_argument("--seq", type=int, default=1)
    can_tx_parser.add_argument("--id", required=True, help="11-bit standard CAN ID, for example 0x601")
    can_tx_parser.add_argument("--hex", required=True, help="CAN data bytes, for example: 11 22 33 44")
    can_tx_parser.add_argument("--encoding", default="utf-8")
    can_tx_parser.add_argument("--timeout-ms", type=int, default=1000)
    can_tx_parser.add_argument("--dtr", choices=["on", "off"], default=None)
    can_tx_parser.add_argument("--rts", choices=["on", "off"], default=None)
    can_burst_parser = subparsers.add_parser("can-burst", help="Queue repeated classic CAN frames in the F407 Tester")
    can_burst_parser.add_argument("--port", required=True, help="Serial port, for example COM17")
    can_burst_parser.add_argument("--baudrate", type=int, default=9600)
    can_burst_parser.add_argument("--seq", type=int, default=1)
    can_burst_parser.add_argument("--id", required=True, help="11-bit standard CAN ID, for example 0x601")
    can_burst_parser.add_argument("--hex", required=True, help="One CAN data frame, for example: 03 22 F1 90 00 00 00 00")
    can_burst_parser.add_argument("--count", type=int, default=8, help="Number of copies to enqueue, 1..32")
    can_burst_parser.add_argument("--encoding", default="utf-8")
    can_burst_parser.add_argument("--timeout-ms", type=int, default=1000)
    can_burst_parser.add_argument("--dtr", choices=["on", "off"], default=None)
    can_burst_parser.add_argument("--rts", choices=["on", "off"], default=None)
    can_poll_parser = subparsers.add_parser("can-poll", help="Poll one buffered CAN response frame from the Tester node")
    can_poll_parser.add_argument("--port", required=True, help="Serial port, for example COM11")
    can_poll_parser.add_argument("--baudrate", type=int, default=9600)
    can_poll_parser.add_argument("--seq", type=int, default=1)
    can_poll_parser.add_argument("--encoding", default="utf-8")
    can_poll_parser.add_argument("--timeout-ms", type=int, default=1000)
    can_poll_parser.add_argument("--dtr", choices=["on", "off"], default=None)
    can_poll_parser.add_argument("--rts", choices=["on", "off"], default=None)
    can_drain_parser = subparsers.add_parser("can-drain", help="Drain buffered CAN RX frames from the F407 Tester")
    can_drain_parser.add_argument("--port", required=True, help="Serial port, for example COM17")
    can_drain_parser.add_argument("--baudrate", type=int, default=9600)
    can_drain_parser.add_argument("--seq", type=int, default=1)
    can_drain_parser.add_argument("--max-frames", type=int, default=32)
    can_drain_parser.add_argument("--timeout-ms", type=int, default=1000)
    can_drain_parser.add_argument("--dtr", choices=["on", "off"], default=None)
    can_drain_parser.add_argument("--rts", choices=["on", "off"], default=None)
    uds_parser = subparsers.add_parser("uds", help="Send one UDS request via CAN-TP Single Frame through the Tester node")
    uds_parser.add_argument("--port", required=True, help="Serial port, for example COM11")
    uds_parser.add_argument("--baudrate", type=int, default=9600)
    uds_parser.add_argument("--seq", type=int, default=1)
    uds_parser.add_argument("--id", default="0x601", help="11-bit request CAN ID, default 0x601")
    uds_parser.add_argument("--response-id", type=lambda value: int(value, 0), default=0x602)
    uds_parser.add_argument("--hex", required=True, help="UDS payload bytes, for example: 22 F1 90")
    uds_parser.add_argument("--encoding", default="utf-8")
    uds_parser.add_argument("--timeout-ms", type=int, default=1500)
    uds_parser.add_argument("--poll-interval-ms", type=int, default=50)
    uds_parser.add_argument("--dtr", choices=["on", "off"], default=None)
    uds_parser.add_argument("--rts", choices=["on", "off"], default=None)
    uds_multi_parser = subparsers.add_parser(
        "uds-multi",
        help="Send an SF UDS request and receive a CAN-TP multi-frame response",
    )
    uds_multi_parser.add_argument("--port", required=True, help="Serial port, for example COM17")
    uds_multi_parser.add_argument("--baudrate", type=int, default=9600)
    uds_multi_parser.add_argument("--seq", type=int, default=1)
    uds_multi_parser.add_argument("--id", default="0x601", help="11-bit request CAN ID, default 0x601")
    uds_multi_parser.add_argument("--response-id", type=lambda value: int(value, 0), default=0x602)
    uds_multi_parser.add_argument("--hex", required=True, help="SF UDS request payload, for example: 22 F1 92")
    uds_multi_parser.add_argument("--encoding", default="utf-8")
    uds_multi_parser.add_argument("--timeout-ms", type=int, default=1500)
    uds_multi_parser.add_argument("--poll-interval-ms", type=int, default=10)
    uds_multi_parser.add_argument("--fc-block-size", type=int, default=0,
                                  help="CTS Block Size sent to ECU for its multi-frame response (0 = unlimited)")
    uds_multi_parser.add_argument("--fc-stmin", type=int, default=0,
                                  help="CTS STmin byte sent to ECU (0..127 ms; default 0)")
    uds_multi_parser.add_argument("--dtr", choices=["on", "off"], default=None)
    uds_multi_parser.add_argument("--rts", choices=["on", "off"], default=None)
    uds_multi_tx_parser = subparsers.add_parser(
        "uds-multi-tx",
        help="Send an FF/CF UDS request, honour ECU FC, and receive its UDS response",
    )
    uds_multi_tx_parser.add_argument("--port", required=True, help="Serial port, for example COM17")
    uds_multi_tx_parser.add_argument("--baudrate", type=int, default=9600)
    uds_multi_tx_parser.add_argument("--seq", type=int, default=1)
    uds_multi_tx_parser.add_argument("--id", default="0x601", help="11-bit request CAN ID, default 0x601")
    uds_multi_tx_parser.add_argument("--response-id", type=lambda value: int(value, 0), default=0x602)
    uds_multi_tx_parser.add_argument("--hex", required=True, help="UDS PDU longer than 7 bytes")
    uds_multi_tx_parser.add_argument("--encoding", default="utf-8")
    uds_multi_tx_parser.add_argument("--timeout-ms", type=int, default=1500)
    uds_multi_tx_parser.add_argument("--poll-interval-ms", type=int, default=10)
    uds_multi_tx_parser.add_argument("--dtr", choices=["on", "off"], default=None)
    uds_multi_tx_parser.add_argument("--rts", choices=["on", "off"], default=None)
    uds_negative_parser = subparsers.add_parser(
        "uds-multi-negative",
        help="Inject bad CF sequence number or CF timeout after a CAN-TP First Frame",
    )
    uds_negative_parser.add_argument("--port", required=True, help="Serial port, for example COM17")
    uds_negative_parser.add_argument("--baudrate", type=int, default=9600)
    uds_negative_parser.add_argument("--seq", type=int, default=1)
    uds_negative_parser.add_argument("--id", default="0x601", help="11-bit request CAN ID, default 0x601")
    uds_negative_parser.add_argument("--response-id", type=lambda value: int(value, 0), default=0x602)
    uds_negative_parser.add_argument("--hex", required=True, help="UDS PDU longer than 7 bytes")
    uds_negative_parser.add_argument("--mode", choices=["bad-sn", "timeout"], required=True)
    uds_negative_parser.add_argument("--wait-ms", type=int, default=1100)
    uds_negative_parser.add_argument("--encoding", default="utf-8")
    uds_negative_parser.add_argument("--timeout-ms", type=int, default=1500)
    uds_negative_parser.add_argument("--dtr", choices=["on", "off"], default=None)
    uds_negative_parser.add_argument("--rts", choices=["on", "off"], default=None)
    stress_parser = subparsers.add_parser(
        "uds-stress",
        help="Run sequential UDS-over-CAN Single Frame round trips on one UART session",
    )
    stress_parser.add_argument("--port", required=True, help="Serial port, for example COM17")
    stress_parser.add_argument("--baudrate", type=int, default=9600)
    stress_parser.add_argument("--seq", type=int, default=1)
    stress_parser.add_argument("--id", default="0x601", help="11-bit request CAN ID, default 0x601")
    stress_parser.add_argument("--response-id", type=lambda value: int(value, 0), default=0x602)
    stress_parser.add_argument("--hex", required=True, help="UDS request payload, for example: 22 F1 90")
    stress_parser.add_argument("--expected", required=True, help="Expected UDS response bytes")
    stress_parser.add_argument("--count", type=int, default=20)
    stress_parser.add_argument("--timeout-ms", type=int, default=1500)
    stress_parser.add_argument("--poll-interval-ms", type=int, default=10)
    stress_parser.add_argument("--inter-request-ms", type=int, default=0)
    stress_parser.add_argument("--dtr", choices=["on", "off"], default=None)
    stress_parser.add_argument("--rts", choices=["on", "off"], default=None)

    args = parser.parse_args()
    if args.command == "list":
        return list_ports()
    if args.command == "listen":
        return listen(args)
    if args.command == "transact":
        return transact(args)
    if args.command in (
        "ping", "info", "runtime", "trace-stats", "trace-bad-crc",
        "can-tx", "can-burst", "can-poll",
    ):
        return protocol_command(args)
    if args.command == "uds":
        return uds_request(args)
    if args.command == "uds-multi":
        return uds_multi_request(args)
    if args.command == "uds-multi-tx":
        return uds_multi_tx_request(args)
    if args.command == "uds-multi-negative":
        return uds_multi_negative_request(args)
    if args.command == "uds-stress":
        if args.count < 1:
            return emit({"ok": False, "error": "--count must be at least 1."}, code=2)
        if args.inter_request_ms < 0:
            return emit({"ok": False, "error": "--inter-request-ms cannot be negative."}, code=2)
        return uds_stress(args)
    if args.command == "can-drain":
        if args.max_frames < 1:
            return emit({"ok": False, "error": "--max-frames must be at least 1."}, code=2)
        return can_drain(args)

    return emit({"ok": False, "error": f"Unsupported command: {args.command}"}, code=2)


if __name__ == "__main__":
    sys.exit(main())
