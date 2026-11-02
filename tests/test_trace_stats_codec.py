from __future__ import annotations

import sys
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parents[1] / "tools" / "pc_diag"
sys.path.insert(0, str(TOOLS_DIR))

from serial_bridge import parse_trace_stats_payload  # noqa: E402


def test_trace_stats_payload() -> None:
    payload = bytearray((1, 2, 1, 3, 7))
    values = (10, 9, 1, 18, 0, 2, 2, 44, 1)
    for value in values:
        payload.extend(value.to_bytes(4, "little"))

    decoded = parse_trace_stats_payload(bytes(payload))
    assert decoded["traceStatsVersion"] == 1
    assert decoded["state"] == 2
    assert decoded["readyInput"] is True
    assert decoded["queuePending"] == 3
    assert decoded["queueHighWater"] == 7
    assert decoded["framesEnqueued"] == 10
    assert decoded["framesSent"] == 9
    assert decoded["badCrcInjected"] == 1


def test_trace_stats_short_payload_is_reported() -> None:
    decoded = parse_trace_stats_payload(b"\x01\x02")
    assert "parseWarning" in decoded
