from __future__ import annotations

from dataclasses import dataclass


SOF = b"\xA5\x5A"
VERSION = 0x01
MAX_PAYLOAD_SIZE = 128
MIN_BODY_SIZE = 3
MAX_BODY_SIZE = MIN_BODY_SIZE + MAX_PAYLOAD_SIZE

CMD_PING = 0x01
CMD_GET_INFO = 0x02
CMD_SET_MODE = 0x03
CMD_CAN_TX = 0x10
CMD_CAN_RX_POLL = 0x11
CMD_GET_RUNTIME = 0x12
CMD_CAN_TX_BURST = 0x13
CMD_TRACE_STATS = 0x14
CMD_TRACE_BAD_CRC = 0x15
CMD_UDS_REQUEST = 0x20
CMD_ERROR = 0xFF


class UartLinkError(ValueError):
    pass


@dataclass(frozen=True)
class UartFrame:
    version: int
    seq: int
    cmd: int
    payload: bytes


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def response_cmd(request_cmd: int) -> int:
    if request_cmd < 0x80:
        return request_cmd | 0x80
    return CMD_ERROR


def build_frame(seq: int, cmd: int, payload: bytes = b"") -> bytes:
    if not 0 <= seq <= 0xFF:
        raise UartLinkError("seq must fit in one byte")
    if not 0 <= cmd <= 0xFF:
        raise UartLinkError("cmd must fit in one byte")
    if len(payload) > MAX_PAYLOAD_SIZE:
        raise UartLinkError(f"payload too large: {len(payload)} > {MAX_PAYLOAD_SIZE}")

    body = bytes([VERSION, seq, cmd]) + payload
    header = SOF + len(body).to_bytes(2, "little")
    crc_input = header[2:] + body
    crc = crc16_ccitt(crc_input).to_bytes(2, "little")
    return header + body + crc


def parse_one(buffer: bytes) -> tuple[UartFrame | None, int]:
    start = buffer.find(SOF)
    if start < 0:
        return None, max(0, len(buffer) - 1)
    if start > 0:
        return None, start
    if len(buffer) < 5:
        return None, 0

    body_len = int.from_bytes(buffer[2:4], "little")
    if body_len < MIN_BODY_SIZE or body_len > MAX_BODY_SIZE:
        return None, 2

    frame_len = 2 + 2 + body_len + 2
    if len(buffer) < frame_len:
        return None, 0

    version = buffer[4]
    if version != VERSION:
        return None, frame_len

    expected_crc = int.from_bytes(buffer[frame_len - 2 : frame_len], "little")
    actual_crc = crc16_ccitt(buffer[2 : 4 + body_len])
    if expected_crc != actual_crc:
        return None, 2

    return (
        UartFrame(
            version=version,
            seq=buffer[5],
            cmd=buffer[6],
            payload=buffer[7 : 4 + body_len],
        ),
        frame_len,
    )
