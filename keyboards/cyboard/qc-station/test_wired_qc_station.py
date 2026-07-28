#!/usr/bin/env python3
"""Unit tests for the pure parsing / verdict / log-path logic in
wired_qc_station.py.

These pin the two firmware-format regexes (RE_RESULT, RE_SURFACE) and the
verdict/log-path helpers so a future change to the firmware's `qc_printf`
output — or to a threshold — fails loudly here instead of silently producing
blank CSV columns or a "no result" on the bench.

Run:  pytest keyboards/cyboard/qc-station/test_wired_qc_station.py
The module imports pyserial at load time; we stub it so the test needs only
pytest (the pure logic under test never touches the serial port).
"""

import importlib.util
import os
import sys
import types

import pytest

# --- Import the station module with `serial` stubbed out ---------------------
# wired_qc_station.py sys.exit()s if pyserial is missing, and pulls in
# serial.tools.list_ports at import. None of the logic we test uses it, so a
# minimal fake keeps the test dependency-free.
_fake_serial = types.ModuleType("serial")
_fake_serial.Serial = object
_fake_serial.SerialException = Exception
_fake_tools = types.ModuleType("serial.tools")
_fake_list_ports = types.ModuleType("serial.tools.list_ports")
_fake_list_ports.comports = lambda: []
_fake_tools.list_ports = _fake_list_ports
sys.modules.setdefault("serial", _fake_serial)
sys.modules.setdefault("serial.tools", _fake_tools)
sys.modules.setdefault("serial.tools.list_ports", _fake_list_ports)

_HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location(
    "wired_qc_station", os.path.join(_HERE, "wired_qc_station.py"))
qc = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(qc)


# --- RE_RESULT: the consistency line -----------------------------------------
# Sample matches the firmware's qc_printf in keymap.c (qc_mon_finish).
FULL_LINE = ("[trackball_left@0] consistency: motion in 98% of 200 x 25ms bins; "
             "longest pause 50ms; path 12345 counts; coherence 87%; "
             "reversals 2/40; bins p10/med/max 30/45/80; slow 1/195")


def test_re_result_full_line():
    m = qc.RE_RESULT.search(FULL_LINE)
    assert m is not None
    assert m.group(1) == "trackball_left@0"
    assert m.group(2) == "98"    # motion_pct
    assert m.group(3) == "200"   # bins
    assert m.group(4) == "50"    # longest_pause_ms
    assert m.group(5) == "12345"  # path_counts
    assert m.group(6) == "87"    # coherence_pct
    assert (m.group(7), m.group(8)) == ("2", "40")     # reversals/dir_pairs
    assert (m.group(9), m.group(10), m.group(11)) == ("30", "45", "80")  # p10/med/max
    assert (m.group(12), m.group(13)) == ("1", "195")  # slow/active_bins


def test_re_result_minimal_line():
    # The tail groups are optional; a bare motion+pause line still parses so a
    # zero-motion run yields a result (FAIL) rather than "no result".
    line = "[trackball_right@1] consistency: motion in 0% of 200 x 25ms bins; longest pause 5000ms"
    m = qc.RE_RESULT.search(line)
    assert m is not None
    assert m.group(2) == "0"
    assert m.group(4) == "5000"
    assert m.group(5) is None    # no path segment


# --- RE_SURFACE / parse_surface ----------------------------------------------
def test_parse_surface_with_lift():
    line = ("[trackball_left@0] surface(rolling): squal 78/82/90 shutter 30/35/41 "
            "pix 0/88/109 lift 3% (48 samples)")
    s = qc.parse_surface(line)
    assert s == {
        "squal_min": 78, "squal_avg": 82, "squal_max": 90,
        "shutter_min": 30, "shutter_avg": 35, "shutter_max": 41,
        "pix_min": 0, "pix_avg": 88, "pix_max": 109,
        "lift_pct": 3, "diag_n": 48,
    }


def test_parse_surface_without_lift():
    # Older/other firmware may omit the lift field; lift_pct falls back to "".
    line = ("[x@0] surface(rolling): squal 70/75/80 shutter 20/25/30 "
            "pix 1/2/3 (10 samples)")
    s = qc.parse_surface(line)
    assert s["lift_pct"] == ""
    assert s["diag_n"] == 10


def test_parse_surface_no_match():
    assert qc.parse_surface("nonsense line") is None


# --- verdict -----------------------------------------------------------------
def _res(**over):
    base = {
        "motion_pct": 99, "longest_pause_ms": 10,
        "active_bins": 200, "slow_bins": 0,
        "bin_med": 45, "bin_p10": 30, "skew_pct": None,
    }
    base.update(over)
    return base


def test_verdict_good():
    assert qc.verdict(_res()) == "GOOD"


def test_verdict_fail_low_motion():
    assert qc.verdict(_res(motion_pct=50)) == "FAIL"


def test_verdict_marginal_band():
    assert qc.verdict(_res(motion_pct=80)) == "MARGINAL"


def test_verdict_long_pause_demotes_good():
    v = qc.verdict(_res(longest_pause_ms=250))
    assert v == "MARGINAL (long pause)"


def test_verdict_dispersion_demotes_good():
    # slow/active above SLOW_FRAC_FLAG demotes an otherwise-GOOD run.
    v = qc.verdict(_res(slow_bins=50, active_bins=200))
    assert v == "MARGINAL (dispersion)"


def test_verdict_skew_suspect_suffix():
    v = qc.verdict(_res(skew_pct=15))
    assert v.startswith("GOOD")
    assert "SUSPECT" in v


def test_verdict_ship_gate_stricter(monkeypatch):
    # A single slow bin passes the default gate but trips --ship-gate.
    monkeypatch.setattr(qc, "SHIP_GATE", False)
    assert qc.verdict(_res(slow_bins=1, active_bins=200)) == "GOOD"
    monkeypatch.setattr(qc, "SHIP_GATE", True)
    assert qc.verdict(_res(slow_bins=1, active_bins=200)) == "MARGINAL (dispersion)"


# --- resolve_log_path --------------------------------------------------------
def test_resolve_log_path_new_file(tmp_path):
    p = str(tmp_path / "runs.csv")
    assert qc.resolve_log_path(p) == p  # non-existent path is fine as-is


def test_resolve_log_path_matching_header(tmp_path):
    p = tmp_path / "runs.csv"
    p.write_text(",".join(qc.CSV_FIELDS) + "\n")
    assert qc.resolve_log_path(str(p)) == str(p)


def test_resolve_log_path_rolls_on_stale_header(tmp_path):
    p = tmp_path / "runs.csv"
    p.write_text("old,columns,that,do,not,match\n")
    rolled = qc.resolve_log_path(str(p))
    assert rolled != str(p)
    assert rolled == str(tmp_path / "runs-2.csv")


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-q"]))
