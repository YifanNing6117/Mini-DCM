import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "pc_diag"))

from minidcm_uart_link import (  # noqa: E402
    CMD_PING,
    SOF,
    VERSION,
    build_frame,
    crc16_ccitt,
    parse_one,
    response_cmd,
)


def test_crc16_ccitt_check_value():
    assert crc16_ccitt(b"123456789") == 0x29B1


def test_build_and_parse_ping_frame():
    raw = build_frame(seq=7, cmd=CMD_PING, payload=b"abc")

    assert raw.startswith(SOF)
    frame, consumed = parse_one(raw)

    assert consumed == len(raw)
    assert frame.version == VERSION
    assert frame.seq == 7
    assert frame.cmd == CMD_PING
    assert frame.payload == b"abc"


def test_parse_discards_noise_before_sof():
    raw = b"\x00\x11" + build_frame(seq=1, cmd=CMD_PING)

    frame, consumed = parse_one(raw)

    assert frame is None
    assert consumed == 2


def test_parse_rejects_bad_crc_and_resyncs_from_sof():
    raw = bytearray(build_frame(seq=1, cmd=CMD_PING, payload=b"x"))
    raw[-1] ^= 0xFF

    frame, consumed = parse_one(bytes(raw))

    assert frame is None
    assert consumed == 2


def test_response_command_mapping():
    assert response_cmd(0x01) == 0x81
    assert response_cmd(0x20) == 0xA0
    assert response_cmd(0x80) == 0xFF
