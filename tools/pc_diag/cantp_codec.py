"""Minimal ISO 15765-2 Classic CAN codec used by MiniDCM host-side tools.

This module deliberately has no serial dependency.  It defines the V2.9-1
wire-level contract and can be tested on the PC before wiring it into the
F407 UART bridge and the F103 ECU transport state machine.
"""

from __future__ import annotations


CAN_DATA_LEN = 8
SINGLE_FRAME_MAX_PAYLOAD = 7
FIRST_FRAME_INITIAL_PAYLOAD = 6
CONSECUTIVE_FRAME_PAYLOAD = 7
MAX_PDU_LEN = 256


class CanTpCodecError(ValueError):
    """A CAN-TP frame violates the MiniDCM V2.9 wire contract."""


def _pad(frame: bytes) -> bytes:
    if len(frame) > CAN_DATA_LEN:
        raise CanTpCodecError("Classic CAN data cannot exceed 8 bytes.")
    return frame + bytes(CAN_DATA_LEN - len(frame))


def encode_single_frame(payload: bytes) -> bytes:
    if not 1 <= len(payload) <= SINGLE_FRAME_MAX_PAYLOAD:
        raise CanTpCodecError("Single Frame payload length must be 1..7 bytes.")
    return _pad(bytes([len(payload)]) + payload)


def encode_first_frame(payload: bytes) -> bytes:
    if not SINGLE_FRAME_MAX_PAYLOAD < len(payload) <= MAX_PDU_LEN:
        raise CanTpCodecError(f"First Frame payload length must be 8..{MAX_PDU_LEN} bytes.")
    length = len(payload)
    pci0 = 0x10 | ((length >> 8) & 0x0F)
    return _pad(bytes([pci0, length & 0xFF]) + payload[:FIRST_FRAME_INITIAL_PAYLOAD])


def encode_consecutive_frame(sequence_number: int, payload_chunk: bytes) -> bytes:
    if not 0 <= sequence_number <= 0x0F:
        raise CanTpCodecError("Consecutive Frame sequence number must be 0..15.")
    if not 1 <= len(payload_chunk) <= CONSECUTIVE_FRAME_PAYLOAD:
        raise CanTpCodecError("Consecutive Frame data length must be 1..7 bytes.")
    return _pad(bytes([0x20 | sequence_number]) + payload_chunk)


def encode_flow_control_cts(block_size: int = 0, st_min: int = 0) -> bytes:
    if not 0 <= block_size <= 0xFF:
        raise CanTpCodecError("Flow Control block size must be 0..255.")
    if not 0 <= st_min <= 0xFF:
        raise CanTpCodecError("Flow Control STmin must be 0..255.")
    return _pad(bytes([0x30, block_size, st_min]))


def segment_pdu(payload: bytes) -> list[bytes]:
    """Encode one PDU into SF or FF/CF classic-CAN frames.

    This is used by the V2.9-3 host client.  Flow Control is intentionally
    handled by the serial transaction layer because it depends on the ECU.
    """
    if not 1 <= len(payload) <= MAX_PDU_LEN:
        raise CanTpCodecError(f"PDU length must be 1..{MAX_PDU_LEN} bytes.")
    if len(payload) <= SINGLE_FRAME_MAX_PAYLOAD:
        return [encode_single_frame(payload)]

    frames = [encode_first_frame(payload)]
    sequence_number = 1
    offset = FIRST_FRAME_INITIAL_PAYLOAD
    while offset < len(payload):
        chunk = payload[offset : offset + CONSECUTIVE_FRAME_PAYLOAD]
        frames.append(encode_consecutive_frame(sequence_number, chunk))
        sequence_number = (sequence_number + 1) & 0x0F
        offset += len(chunk)
    return frames


def frame_type(frame: bytes) -> int:
    if not frame:
        raise CanTpCodecError("Empty CAN data cannot contain a CAN-TP PCI.")
    return (frame[0] >> 4) & 0x0F


def parse_flow_control(frame: bytes) -> tuple[int, int, int]:
    if len(frame) < 3 or frame_type(frame) != 0x03:
        raise CanTpCodecError("Expected a CAN-TP Flow Control frame.")
    return frame[0] & 0x0F, frame[1], frame[2]


def st_min_to_seconds(st_min: int) -> float:
    """Convert ISO-TP STmin encoding to a host delay in seconds."""
    if 0 <= st_min <= 0x7F:
        return st_min / 1000.0
    if 0xF1 <= st_min <= 0xF9:
        return (st_min - 0xF0) / 10000.0
    raise CanTpCodecError(f"Unsupported ISO-TP STmin encoding 0x{st_min:02X}.")


class CanTpReassembler:
    """Reassembles one CAN-TP PDU and exposes the FC required after an FF."""

    def __init__(self, max_pdu_len: int = MAX_PDU_LEN, block_size: int = 0, st_min: int = 0) -> None:
        if not 8 <= max_pdu_len <= MAX_PDU_LEN:
            raise ValueError(f"max_pdu_len must be 8..{MAX_PDU_LEN}.")
        if not 0 <= block_size <= 0xFF:
            raise ValueError("block_size must be 0..255.")
        if not 0 <= st_min <= 0xFF:
            raise ValueError("st_min must be 0..255.")
        self.max_pdu_len = max_pdu_len
        self.block_size = block_size
        self.st_min = st_min
        self.reset()

    def reset(self) -> None:
        self._buffer = bytearray()
        self._expected_len = 0
        self._expected_sn = 1
        self._waiting_for_cf = False
        self._cf_in_block = 0

    @property
    def waiting_for_cf(self) -> bool:
        return self._waiting_for_cf

    @property
    def expected_sequence_number(self) -> int:
        return self._expected_sn

    def feed(self, frame: bytes) -> tuple[bytes | None, bytes | None]:
        """Feed one CAN frame.

        Returns ``(completed_pdu, flow_control_frame)``.  A First Frame returns
        an FC(CTS); a completed SF or CF returns the reassembled PDU.
        """
        if not frame:
            raise CanTpCodecError("Empty CAN frame.")

        kind = frame_type(frame)
        if kind == 0x00:
            return self._feed_single_frame(frame), None
        if kind == 0x01:
            return self._feed_first_frame(frame), encode_flow_control_cts(self.block_size, self.st_min)
        if kind == 0x02:
            return self._feed_consecutive_frame(frame)
        raise CanTpCodecError(f"Unsupported received CAN-TP frame type 0x{kind:X}.")

    def _feed_single_frame(self, frame: bytes) -> bytes:
        length = frame[0] & 0x0F
        if not 1 <= length <= SINGLE_FRAME_MAX_PAYLOAD or len(frame) < length + 1:
            raise CanTpCodecError("Invalid Single Frame length.")
        self.reset()
        return bytes(frame[1 : 1 + length])

    def _feed_first_frame(self, frame: bytes) -> None:
        if len(frame) < CAN_DATA_LEN:
            raise CanTpCodecError("First Frame must contain 8 CAN data bytes.")
        total_len = ((frame[0] & 0x0F) << 8) | frame[1]
        if not SINGLE_FRAME_MAX_PAYLOAD < total_len <= self.max_pdu_len:
            raise CanTpCodecError("First Frame total length is outside configured bounds.")
        self._buffer = bytearray(frame[2 : 2 + FIRST_FRAME_INITIAL_PAYLOAD])
        self._expected_len = total_len
        self._expected_sn = 1
        self._waiting_for_cf = True
        return None

    def _feed_consecutive_frame(self, frame: bytes) -> bytes | None:
        if not self._waiting_for_cf:
            raise CanTpCodecError("Consecutive Frame received without an active First Frame.")
        received_sn = frame[0] & 0x0F
        if received_sn != self._expected_sn:
            self.reset()
            raise CanTpCodecError(
                f"Unexpected Consecutive Frame SN {received_sn}; expected {self._expected_sn}."
            )
        remaining = self._expected_len - len(self._buffer)
        if remaining <= 0:
            self.reset()
            raise CanTpCodecError("Consecutive Frame received after PDU completion.")
        chunk_len = min(remaining, CONSECUTIVE_FRAME_PAYLOAD)
        if len(frame) < 1 + chunk_len:
            self.reset()
            raise CanTpCodecError("Consecutive Frame does not contain the required payload bytes.")

        self._buffer.extend(frame[1 : 1 + chunk_len])
        self._expected_sn = (self._expected_sn + 1) & 0x0F
        self._cf_in_block += 1
        if len(self._buffer) < self._expected_len:
            if self.block_size and self._cf_in_block >= self.block_size:
                self._cf_in_block = 0
                return None, encode_flow_control_cts(self.block_size, self.st_min)
            return None, None

        completed = bytes(self._buffer)
        self.reset()
        return completed, None
