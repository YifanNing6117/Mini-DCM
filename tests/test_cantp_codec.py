import sys
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "pc_diag"))

from cantp_codec import (  # noqa: E402
    CanTpCodecError,
    CanTpReassembler,
    encode_consecutive_frame,
    encode_first_frame,
    encode_flow_control_cts,
    encode_single_frame,
    parse_flow_control,
    segment_pdu,
    st_min_to_seconds,
)


def test_single_frame_codec_contract():
    assert encode_single_frame(b"\x22\xF1\x90") == bytes.fromhex("03 22 F1 90 00 00 00 00")


def test_first_frame_length_and_initial_payload():
    payload = bytes(range(20))
    assert encode_first_frame(payload) == bytes.fromhex("10 14 00 01 02 03 04 05")


def test_flow_control_cts_contract():
    fc = encode_flow_control_cts()
    assert fc == bytes.fromhex("30 00 00 00 00 00 00 00")
    assert parse_flow_control(fc) == (0, 0, 0)
    assert st_min_to_seconds(10) == pytest.approx(0.010)
    assert st_min_to_seconds(0xF1) == pytest.approx(0.0001)


def test_reassembler_returns_fc_then_completed_multi_frame_pdu():
    payload = b"MiniDCM CAN-TP V2.9"  # 19 bytes: one FF, two CF
    receiver = CanTpReassembler()

    completed, fc = receiver.feed(encode_first_frame(payload))
    assert completed is None
    assert fc == encode_flow_control_cts()
    assert receiver.waiting_for_cf
    assert receiver.expected_sequence_number == 1

    completed, fc = receiver.feed(encode_consecutive_frame(1, payload[6:13]))
    assert completed is None
    assert fc is None

    completed, fc = receiver.feed(encode_consecutive_frame(2, payload[13:]))
    assert completed == payload
    assert fc is None
    assert not receiver.waiting_for_cf


def test_reassembler_rejects_bad_consecutive_frame_sn():
    receiver = CanTpReassembler()
    receiver.feed(encode_first_frame(bytes(range(12))))

    with pytest.raises(CanTpCodecError, match="Unexpected Consecutive Frame SN"):
        receiver.feed(encode_consecutive_frame(2, bytes(range(6, 12))))


def test_reassembler_emits_repeated_flow_control_for_nonzero_block_size():
    payload = bytes(range(27))  # FF + three CF frames
    receiver = CanTpReassembler(block_size=2)

    completed, fc = receiver.feed(encode_first_frame(payload))
    assert completed is None
    assert fc == encode_flow_control_cts(2, 0)

    completed, fc = receiver.feed(encode_consecutive_frame(1, payload[6:13]))
    assert completed is None
    assert fc is None

    completed, fc = receiver.feed(encode_consecutive_frame(2, payload[13:20]))
    assert completed is None
    assert fc == encode_flow_control_cts(2, 0)

    completed, fc = receiver.feed(encode_consecutive_frame(3, payload[20:]))
    assert completed == payload
    assert fc is None


def test_codec_rejects_invalid_lengths():
    with pytest.raises(CanTpCodecError):
        encode_first_frame(b"1234567")
    with pytest.raises(CanTpCodecError):
        encode_consecutive_frame(1, b"")


def test_segment_pdu_generates_ff_and_cf_for_multiframe_request():
    payload = bytes.fromhex("2E F1 93 4D 55 4C 54 49 46 52 41 4D 45 2D 52 58 2D 4F 4B")

    assert segment_pdu(payload) == [
        bytes.fromhex("10 13 2E F1 93 4D 55 4C"),
        bytes.fromhex("21 54 49 46 52 41 4D 45"),
        bytes.fromhex("22 2D 52 58 2D 4F 4B 00"),
    ]
