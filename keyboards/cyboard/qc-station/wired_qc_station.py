#!/usr/bin/env python3
"""Cyboard wired-trackball QC station — PMW3360 consistency test runner.

The wired counterpart of the wireless imprint_qc_station.py (which drives the
PMW3610 over ZMK). Talks to a wired Cyboard half running the QC test firmware
(`make cyboard/imprint/tester:qc`) over its USB CDC virtual-serial port and
drives the `qc mon` consistency meter: you roll the ball in circles for a few
seconds and it reports what fraction of the window had motion, the longest
pause, total path counts, direction coherence, reversals, and bin-magnitude
dispersion. Higher motion % + shorter pause + high coherence = smoother
tracking.

The wired PMW3360 has been much more consistent than the wireless PMW3610, but
this gives us the same objective pass/fail and a CSV trail to prove it — plus
two PMW3360-only surface reads folded into the roll: whole-array brightness
(Raw_Data_Sum, reported as `pix` avg) and a lift-detect fraction (`lift%`).

It can also auto-flash the QC firmware: plug in a half, it asks the board to
enter the RP2040 UF2 bootloader (`qc boot`, with a double-tap-RESET fallback)
and copies the firmware onto the RPI-RP2 drive.

Usage:
    python3 wired_qc_station.py                 # plug in a half; auto-flash + test
    python3 wired_qc_station.py --no-flash      # use whatever firmware is on it
    python3 wired_qc_station.py --port /dev/ttyACM0
    python3 wired_qc_station.py --log runs.csv
    python3 wired_qc_station.py --duration 8

Campaign mode — pin the run metadata on the CLI when only the unit under test
changes, so each cycle only asks for the board/sensor label:

    python3 wired_qc_station.py --tester erik --log batch.csv \\
        --ball-color black --ball-brand perixx --surface clean --height standard

One-time setup: use the vial-qmk Nix shell (`nix-shell` at the repo root) which
provides Python + pyserial, or `python3 -m pip install pyserial`.
"""

import argparse
import csv
import glob
import os
import re
import shutil
import subprocess
import sys
import time
from datetime import datetime

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit(
        "This tool needs pyserial. In the vial-qmk repo run it inside the Nix "
        "shell:\n"
        "    nix-shell        # from the repo root, then:\n"
        "    python3 keyboards/cyboard/qc-station/wired_qc_station.py\n"
        "(non-Nix fallback: python3 -m pip install pyserial)"
    )

# ---------------------------------------------------------------------------
# Config / defaults
# ---------------------------------------------------------------------------

DEFAULT_DURATION = 5          # seconds of rolling per measurement
DEFAULT_HEIGHT = "standard"   # nominal lens-to-ball gap
BAUD = 115200                 # CDC-ACM ignores baud, but pyserial wants one.

# The QC test firmware we auto-flash. One image works on either half.
DEFAULT_UF2 = "cyboard_imprint_tester_qc.uf2"

# Volume label the RP2040 UF2 bootloader presents.
UF2_VOLUME_LABELS = ("RPI-RP2",)

BALL_COLORS = ["black", "red", "green", "gold", "silver", "blue", "white", "other"]

# Verdict thresholds. Seeded from the PMW3610 calibration (2026-07-16); the
# wired PMW3360 tracks more consistently, so these are a conservative STARTING
# point — recalibrate against in-hand feel on wired units (run a known-good
# black ball as the per-unit baseline first). GOOD = near-perfect consistency;
# MARGINAL = moves but not shippable.
GOOD_PCT = 97
MARGINAL_PCT = 75
PAUSE_FLAG_MS = 100
# Bin-magnitude dispersion gate (intermittent-slowdown / micro-stall detector).
SLOW_FRAC_FLAG = 0.03      # slow_bins / active_bins above this demotes GOOD
P10_MED_FLAG = 0.4         # bin_p10 below this fraction of bin_med demotes GOOD

# --ship-gate: strict customer-flawless bar. Run each ball twice; reject only if
# flagged in both.
SHIP_SLOW_BINS_MAX = 0
SHIP_P10_MED_FLAG = 0.7
SHIP_GATE = False          # set from args in main()

# Result line from the firmware, e.g.:
#   [trackball_left@0] consistency: motion in 99% of 200 x 25ms bins; longest
#   pause 0ms; path 4212 counts; coherence 100%; reversals 0/181; bins
#   p10/med/max 12/21/44; slow 0/181
RE_RESULT = re.compile(
    r"\[([^\]]+)\]\s+consistency: motion in (\d+)% of (\d+) x \d+ms bins; "
    r"longest pause (\d+)ms(?:; path (\d+) counts)?(?:; coherence (\d+)%)?"
    r"(?:; reversals (\d+)/(\d+))?"
    r"(?:; bins p10/med/max (\d+)/(\d+)/(\d+))?(?:; slow (\d+)/(\d+))?"
)
# Unit serial from `qc id`: "unit_serial=abc123... (…)".
RE_ID = re.compile(r"unit_serial=([0-9a-fA-F]+)")
# Timing-skew warning => the measurement window was stretched; redo.
RE_SKEW = re.compile(r"timing skew \+(\d+)%")
# `qc list` rows, e.g. "0: trackball_left@0".
RE_LIST = re.compile(r"^\s*(\d+):\s+(\S+)(\s+\(not ready\))?\s*$")
# Zephyr-style ANSI escapes are absent here, but strip them defensively.
RE_ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
# During-roll surface summary the firmware prints after the consistency line
# (sampled while moving, so motion-independent). `lift L%` is the PMW3360-only
# lift-detect fraction; older/other firmware may omit it.
#   [trackball_left@0] surface(rolling): squal 78/82/90 shutter 30/35/41
#   pix 0/88/109 lift 0% (48 samples)
RE_SURFACE = re.compile(
    r"surface\(rolling\): squal (\d+)/(\d+)/(\d+) shutter (\d+)/(\d+)/(\d+) "
    r"pix (\d+)/(\d+)/(\d+)(?: lift (\d+)%)? \((\d+) samples\)"
)

SURFACE_KEYS = ("squal_min", "squal_avg", "squal_max",
                "shutter_min", "shutter_avg", "shutter_max",
                "pix_min", "pix_avg", "pix_max", "lift_pct")


def parse_surface(line):
    """Parse the firmware's during-roll surface summary into columns, or None."""
    m = RE_SURFACE.search(line)
    if not m:
        return None
    g = m.groups()
    return {
        "squal_min": int(g[0]), "squal_avg": int(g[1]), "squal_max": int(g[2]),
        "shutter_min": int(g[3]), "shutter_avg": int(g[4]), "shutter_max": int(g[5]),
        "pix_min": int(g[6]), "pix_avg": int(g[7]), "pix_max": int(g[8]),
        "lift_pct": int(g[9]) if g[9] is not None else "",
        "diag_n": int(g[10]),
    }


# ---------------------------------------------------------------------------
# Serial port discovery
# ---------------------------------------------------------------------------

def cdc_ports():
    """USB CDC-ACM serial ports (the keyboard), best-looking first."""
    ports = []
    for p in list_ports.comports():
        dev = (p.device or "").lower()
        is_usb = getattr(p, "vid", None) is not None
        looks_acm = any(k in dev for k in ("ttyacm", "usbmodem", "ttyusb"))
        if is_usb or looks_acm:
            ports.append(p)

    def score(p):
        blob = " ".join(str(x) for x in (p.device, p.description, p.manufacturer)).lower()
        s = 0
        if any(k in blob for k in ("acm", "usbmodem", "cyboard", "tester",
                                   "rp2040", "pico", "imprint", "dactyl")):
            s += 2
        return s

    ports.sort(key=score, reverse=True)
    return ports


def pick_port(explicit):
    if explicit:
        return explicit
    ports = cdc_ports()
    if not ports:
        sys.exit(
            "No serial ports found. Plug the keyboard half into USB and make "
            "sure it's flashed with the QC test firmware, then rerun."
        )
    if len(ports) == 1:
        return ports[0].device
    print("\nMultiple serial ports found:")
    for i, p in enumerate(ports):
        print(f"  [{i}] {p.device}  {p.description}")
    while True:
        raw = input(f"Pick a port [0-{len(ports)-1}] (Enter for 0): ").strip()
        if raw == "":
            return ports[0].device
        if raw.isdigit() and int(raw) < len(ports):
            return ports[int(raw)].device
        print("  ...not a valid choice.")


def wait_for_serial_port(timeout, exclude=None):
    deadline = time.time() + timeout
    while time.time() < deadline:
        for p in cdc_ports():
            if exclude and p.device == exclude:
                continue
            return p.device
        time.sleep(0.5)
    return None


# ---------------------------------------------------------------------------
# Auto-flash via the RP2040 UF2 bootloader (RPI-RP2 mass-storage drive).
# ---------------------------------------------------------------------------

def uf2_drive_candidates():
    dirs = []
    if os.path.exists("/proc/mounts"):
        with open("/proc/mounts") as f:
            for line in f:
                parts = line.split()
                if len(parts) >= 2 and parts[0].startswith("/dev/"):
                    dirs.append(parts[1].replace("\\040", " "))
    dirs.extend(glob.glob("/Volumes/*"))  # macOS
    return [d for d in dirs if os.path.isfile(os.path.join(d, "INFO_UF2.TXT"))]


def unmounted_uf2_devices():
    devs = []
    for path in glob.glob("/dev/disk/by-label/*"):
        label = os.path.basename(path).upper()
        if any(known in label for known in UF2_VOLUME_LABELS):
            devs.append(os.path.realpath(path))
    return devs


def try_udisks_mount(dev):
    if not shutil.which("udisksctl"):
        return None
    r = subprocess.run(["udisksctl", "mount", "-b", dev],
                       capture_output=True, text=True)
    if r.returncode != 0:
        return None
    m = re.search(r" at (.+?)\.?\s*$", r.stdout.strip())
    return m.group(1) if m else None


def wait_for_uf2_drive(timeout, forced=None):
    deadline = time.time() + timeout
    last_heartbeat = 0.0
    while time.time() < deadline:
        if forced and os.path.isfile(os.path.join(forced, "INFO_UF2.TXT")):
            return forced
        found = uf2_drive_candidates()
        if found:
            return found[0]
        for dev in unmounted_uf2_devices():
            mp = try_udisks_mount(dev)
            if mp and os.path.isfile(os.path.join(mp, "INFO_UF2.TXT")):
                return mp
        now = time.time()
        if now - last_heartbeat >= 8:
            last_heartbeat = now
            sys.stdout.write(f"\r  … waiting for the bootloader drive "
                             f"(label {UF2_VOLUME_LABELS[0]})   ")
            sys.stdout.flush()
        time.sleep(0.5)
    return None


def wait_uf2_drive_gone(drive, timeout=60):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if not os.path.isfile(os.path.join(drive, "INFO_UF2.TXT")):
            return True
        time.sleep(0.5)
    return False


def flash_uf2(uf2_path, drive):
    dest = os.path.join(drive, os.path.basename(uf2_path))
    print(f"  copying {os.path.basename(uf2_path)} → {drive} …")
    try:
        shutil.copyfile(uf2_path, dest)
    except OSError as e:
        # RP2040's bootloader only reboots on a complete image, so a failed
        # copy leaves the board safely in DFU — report and let the caller retry.
        print(f"  copy failed: {e}")
        return False
    try:
        fd = os.open(dest, os.O_RDONLY)
        os.fsync(fd)
        os.close(fd)
    except OSError:
        pass
    os.sync()
    return True


def request_dfu(port):
    """Ask a QC-firmware board to reboot into the UF2 bootloader (`qc boot`).
    Harmless no-op on other firmware (prints an unknown-command line we ignore)."""
    try:
        ser = serial.Serial(port, BAUD, timeout=0.1)
    except (serial.SerialException, OSError):
        return
    try:
        drain(ser, 0.2)
        send(ser, "qc boot")
        time.sleep(0.4)
    finally:
        try:
            ser.close()
        except (OSError, serial.SerialException):
            pass


def find_qc_uf2(explicit):
    if explicit:
        if not os.path.isfile(explicit):
            sys.exit(f"--uf2 file not found: {explicit}")
        return explicit
    here = os.path.dirname(os.path.abspath(__file__))
    # repo root is three levels up from keyboards/cyboard/qc-station/
    repo_root = os.path.abspath(os.path.join(here, "..", "..", ".."))
    for base in (here, os.getcwd(), repo_root):
        cand = os.path.join(base, DEFAULT_UF2)
        if os.path.isfile(cand):
            return cand
    return None


def try_open_detect(port, tries=4):
    """Open `port`, probe the qc shell; return (open_serial, sensors) or None."""
    for _ in range(tries):
        dropped = False
        try:
            ser = serial.Serial(port, BAUD, timeout=0.1)
        except (serial.SerialException, OSError):
            ser, dropped = None, True
        if ser is not None:
            time.sleep(0.8)
            try:
                sensors = detect_sensors(ser)
            except (OSError, serial.SerialException):
                sensors, dropped = [], True
            if sensors:
                return ser, sensors
            try:
                ser.close()
            except (OSError, serial.SerialException):
                pass
            if not dropped:
                return None
        newport = wait_for_serial_port(8)
        if newport:
            port = newport
        else:
            time.sleep(0.5)
    return None


def flash_test_firmware(uf2, forced_drive):
    print("\n--- Flashing QC test firmware ---")
    ports = cdc_ports()
    if ports:
        print("  asking the board to enter its bootloader…")
        request_dfu(ports[0].device)
    drive = wait_for_uf2_drive(8, forced=forced_drive)
    if not drive:
        sys.stdout.write("\r" + " " * 60 + "\r")
        print("  >> Double-tap the RESET button on the board now.")
        print("     (a USB drive named RPI-RP2 should appear)")
        drive = wait_for_uf2_drive(120, forced=forced_drive)
    sys.stdout.write("\r" + " " * 60 + "\r")
    if not drive:
        print("  Gave up waiting for the bootloader drive. Is the board plugged in?")
        return None
    print(f"  bootloader drive: {drive}")
    if not flash_uf2(uf2, drive):
        return None
    if not wait_uf2_drive_gone(drive):
        print("  bootloader drive didn't disappear; the flash may not have taken.")
    print("  flashed; waiting for the board to reboot…")
    port = wait_for_serial_port(30)
    if not port:
        print("  Board didn't re-appear as a USB serial port after flashing.")
        return None
    time.sleep(1.5)
    print(f"  back as {port}\n")
    return port


def ensure_test_firmware(uf2, forced_drive, reflash):
    if not reflash:
        ports = cdc_ports()
        if ports:
            got = try_open_detect(ports[0].device)
            if got:
                print(f"  QC test firmware already present on {ports[0].device} "
                      f"— skipping flash.")
                return got
    port = flash_test_firmware(uf2, forced_drive)
    if port is None:
        return None
    return try_open_detect(port)


# ---------------------------------------------------------------------------
# Serial I/O
# ---------------------------------------------------------------------------

def drain(ser, seconds=0.4):
    end = time.time() + seconds
    while time.time() < end:
        try:
            if ser.in_waiting:
                ser.read(ser.in_waiting)
        except (OSError, serial.SerialException):
            return
        time.sleep(0.02)


def send(ser, line):
    try:
        ser.write((line + "\r\n").encode())
        ser.flush()
    except (OSError, serial.SerialException):
        pass


def read_lines_until(ser, deadline, on_line=None):
    buf = b""
    while time.time() < deadline:
        try:
            chunk = ser.read(ser.in_waiting or 1)
        except (OSError, serial.SerialException):
            return
        if chunk:
            buf += chunk
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                line = RE_ANSI.sub("", raw.decode(errors="replace").rstrip("\r"))
                if on_line:
                    on_line(line)
                yield line
        else:
            time.sleep(0.02)


def detect_sensors(ser, attempts=6):
    """Run `qc list` and return [(idx, name, ready), ...]."""
    for _ in range(attempts):
        drain(ser, 0.3)
        send(ser, "")
        send(ser, "qc list")
        found = []
        for line in read_lines_until(ser, time.time() + 1.2):
            m = RE_LIST.match(line)
            if m:
                found.append((int(m.group(1)), m.group(2), m.group(3) is None))
        if found:
            return found
        time.sleep(0.3)
    return []


def read_unit_serial(ser):
    drain(ser, 0.2)
    send(ser, "qc id")
    for line in read_lines_until(ser, time.time() + 1.0):
        m = RE_ID.search(line)
        if m:
            return m.group(1)
    return ""


# ---------------------------------------------------------------------------
# Measurement
# ---------------------------------------------------------------------------

def run_mon(ser, duration, idx):
    """Fire `qc mon` and capture the result. Returns a dict or None."""
    print()
    print("  ┌─────────────────────────────────────────────┐")
    print("  │  ROLL THE BALL IN CIRCLES — keep it moving!  │")
    print("  └─────────────────────────────────────────────┘")
    for n in (3, 2, 1):
        sys.stdout.write(f"\r  starting in {n}… (start rolling now) ")
        sys.stdout.flush()
        time.sleep(1.0)
    sys.stdout.write("\r" + " " * 44 + "\r")
    sys.stdout.flush()

    drain(ser, 0.15)
    send(ser, f"qc mon {duration}")

    deadline = time.time() + duration + 6
    started = time.time()
    result = None
    skew = None
    surface_stats = None
    buf = b""
    shown = None
    while time.time() < deadline and result is None:
        left = max(0, duration - int(time.time() - started))
        if left != shown:
            sys.stdout.write(f"\r  rolling… {left:2d}s remaining ")
            sys.stdout.flush()
            shown = left
        try:
            chunk = ser.read(ser.in_waiting or 1)
        except (OSError, serial.SerialException):
            break
        if not chunk:
            continue
        buf += chunk
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            line = RE_ANSI.sub("", raw.decode(errors="replace").rstrip("\r"))
            ms = RE_SKEW.search(line)
            if ms:
                skew = int(ms.group(1))
            sf = parse_surface(line)
            if sf:
                surface_stats = sf
            m = RE_RESULT.search(line)
            if m:
                result = {
                    "sensor": m.group(1),
                    "motion_pct": int(m.group(2)),
                    "bins": int(m.group(3)),
                    "longest_pause_ms": int(m.group(4)),
                    "path_counts": int(m.group(5)) if m.group(5) else "",
                    "coherence_pct": int(m.group(6)) if m.group(6) else "",
                    "reversals": int(m.group(7)) if m.group(7) else "",
                    "dir_pairs": int(m.group(8)) if m.group(8) else "",
                    "bin_p10": int(m.group(9)) if m.group(9) else "",
                    "bin_med": int(m.group(10)) if m.group(10) else "",
                    "bin_max": int(m.group(11)) if m.group(11) else "",
                    "slow_bins": int(m.group(12)) if m.group(12) else "",
                    "active_bins": int(m.group(13)) if m.group(13) else "",
                }
                # grace read for the trailing skew warning + surface line
                for extra in read_lines_until(ser, time.time() + 0.8):
                    ms2 = RE_SKEW.search(extra)
                    if ms2:
                        skew = int(ms2.group(1))
                    sf2 = parse_surface(extra)
                    if sf2:
                        surface_stats = sf2
                break

    sys.stdout.write("\r" + " " * 44 + "\r")
    sys.stdout.flush()
    if result:
        result["skew_pct"] = skew
        if surface_stats:
            result.update(surface_stats)
        else:
            # (dict spread, not `|`, to stay Python 3.6+ for older bench hosts)
            result.update({**{k: "" for k in SURFACE_KEYS}, "diag_n": 0})
    return result


def verdict(res):
    pct = res["motion_pct"]
    pause = res["longest_pause_ms"]
    if pct >= GOOD_PCT:
        v = "GOOD" if pause <= PAUSE_FLAG_MS else "MARGINAL (long pause)"
    elif pct >= MARGINAL_PCT:
        v = "MARGINAL"
    else:
        v = "FAIL"
    act, slow = res.get("active_bins", ""), res.get("slow_bins", "")
    med, p10 = res.get("bin_med", ""), res.get("bin_p10", "")
    if v == "GOOD" and act not in ("", 0):
        if SHIP_GATE:
            if (slow != "" and slow > SHIP_SLOW_BINS_MAX) or \
               (med not in ("", 0) and p10 != "" and p10 < SHIP_P10_MED_FLAG * med):
                v = "MARGINAL (dispersion)"
        elif (slow != "" and slow / act > SLOW_FRAC_FLAG) or \
             (med not in ("", 0) and p10 != "" and p10 < P10_MED_FLAG * med):
            v = "MARGINAL (dispersion)"
    if res.get("skew_pct") is not None:
        v += "  [!] SUSPECT — timing skew, redo with terminal idle"
    return v


# ---------------------------------------------------------------------------
# Prompts
# ---------------------------------------------------------------------------

def ask(prompt, default=None):
    suffix = f" [{default}]" if default is not None else ""
    raw = input(f"{prompt}{suffix}: ").strip()
    return raw if raw else (default or "")


def ask_color():
    print("Ball color:")
    for i, c in enumerate(BALL_COLORS):
        print(f"  [{i}] {c}")
    while True:
        raw = input(f"  choose [0-{len(BALL_COLORS)-1}] (Enter for 0=black): ").strip()
        if raw == "":
            return BALL_COLORS[0]
        if raw.isdigit() and int(raw) < len(BALL_COLORS):
            choice = BALL_COLORS[int(raw)]
            if choice == "other":
                return ask("  color name") or "other"
            return choice
        print("  ...not a valid choice.")


def ask_surface():
    raw = input("Ball surface [Enter=clean / d=dirty / o=other]: ").strip().lower()
    if raw == "" or raw.startswith("c"):
        return "clean"
    if raw.startswith("d"):
        return "dirty"
    if raw.startswith("o"):
        return ask("  describe surface") or "other"
    return raw


# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------

CSV_FIELDS = [
    "timestamp", "tester", "board_id", "unit_serial", "sensor",
    "ball_color", "ball_brand", "surface", "height", "duration_s",
    "motion_pct", "path_counts", "coherence_pct", "reversals", "dir_pairs",
    "bin_p10", "bin_med", "bin_max", "slow_bins", "active_bins",
    "bins", "longest_pause_ms", "skew_pct",
    # PMW3360 surface diagnostics sampled during the roll (motion-independent).
    # pix_avg == Raw_Data_Sum (whole-array brightness); lift_pct is the sensor's
    # lift-detect fraction. shutter↑ / pix↓ / lift↑ = worse surface or seating.
    "squal_min", "squal_avg", "squal_max",
    "shutter_min", "shutter_avg", "shutter_max",
    "pix_min", "pix_avg", "pix_max", "lift_pct", "diag_n",
    "verdict", "notes",
]


def append_log(path, row):
    new = not os.path.exists(path) or os.path.getsize(path) == 0
    with open(path, "a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=CSV_FIELDS)
        if new:
            w.writeheader()
        w.writerow(row)


def resolve_log_path(path):
    def header_ok(p):
        try:
            with open(p, newline="") as f:
                first = f.readline().rstrip("\n").rstrip("\r")
        except OSError:
            return True
        if first == "":
            return True
        return next(csv.reader([first]), []) == CSV_FIELDS

    if header_ok(path):
        return path
    stem, ext = os.path.splitext(path)
    n = 2
    while os.path.exists(f"{stem}-{n}{ext}") and not header_ok(f"{stem}-{n}{ext}"):
        n += 1
    rolled = f"{stem}-{n}{ext}"
    print(f"!! {path} has an older column layout; logging to {rolled} "
          f"so columns stay aligned.")
    return rolled


# ---------------------------------------------------------------------------
# Per-board session
# ---------------------------------------------------------------------------

def acquire_board(args, uf2):
    if args.port:
        got = try_open_detect(args.port)
    elif uf2 is None:
        got = try_open_detect(pick_port(None))
    else:
        print("Plug in ONE keyboard half via USB (left or right — same firmware).")
        got = ensure_test_firmware(uf2, args.uf2_drive, args.reflash)

    if not got:
        print("!! No PMW3360 sensor responded to `qc list`.")
        print("   The flash may not have taken, or another program grabbed the")
        print("   port (e.g. ModemManager). Unplug/replug the half and retry.")
        return None

    ser, sensors = got
    print("Sensor(s) detected:")
    for idx, name, ready in sensors:
        print(f"  [{idx}] {name}{'' if ready else '  (NOT READY!)'}")
    unit_serial = read_unit_serial(ser)
    if unit_serial:
        print(f"  unit serial: {unit_serial}  (RP2040 uid — logged automatically)")
    if len(sensors) == 1:
        sensor_idx = sensors[0][0]
    else:
        sensor_idx = int(ask("Which sensor index", str(sensors[0][0])) or sensors[0][0])
    return ser, sensor_idx, unit_serial


def measure_loop(ser, sensor_idx, args, tester, board_id, unit_serial, log_path, counter):
    pinned = bool(args.ball_color and args.height)
    print()
    if pinned:
        print(f"Pinned run metadata: {args.ball_color}/{args.ball_brand or 'perixx'} "
              f"({args.surface or 'clean'}) @ {args.height} — no per-run prompts.\n")
    else:
        print("Tip: run a known-good BLACK ball on this unit first as a baseline,")
        print("then test the suspect ball on the same unit and compare motion% AND")
        print("path counts.\n")
    brand_default = "perixx"
    while True:
        color = args.ball_color or ask_color()
        brand = args.ball_brand or ask("Ball brand", brand_default) or brand_default
        brand_default = brand
        surface = args.surface or ask_surface()
        height = args.height or ask(
            "Sensor height (Enter='standard'; type 'shim' if you shimmed it)",
            DEFAULT_HEIGHT)

        res = run_mon(ser, args.duration, sensor_idx)
        if not res:
            print("  !! No result — the ball may not have moved, or the port")
            print("     dropped. Roll continuously the whole time and retry.\n")
            if ask("Retry this measurement? (y/n)", "y").lower().startswith("y"):
                continue
            return "quit"

        v = verdict(res)
        pc = res.get("path_counts", "")
        pc_str = f"   path {pc} counts" if pc != "" else ""
        coh = res.get("coherence_pct", "")
        coh_str = f"   coherence {coh}%" if coh != "" else ""
        rev = res.get("reversals", "")
        rev_str = f"   reversals {rev}/{res.get('dir_pairs','')}" if rev != "" else ""
        med = res.get("bin_med", "")
        if med != "":
            disp_str = (f"   bins p10/med {res.get('bin_p10','')}/{med}"
                        f"   slow {res.get('slow_bins','')}/{res.get('active_bins','')}")
        else:
            disp_str = ""
        print(f"  Result: motion {res['motion_pct']}%{pc_str}{coh_str}{rev_str}{disp_str}   "
              f"longest pause {res['longest_pause_ms']}ms   →  {v}")
        if res.get("diag_n"):
            lift = res.get("lift_pct", "")
            lift_str = f"   lift {lift}%" if lift != "" else ""
            print(f"          shutter {res['shutter_min']}–{res['shutter_max']} "
                  f"(avg {res['shutter_avg']})   pix_min {res['pix_min']}   "
                  f"squal avg {res['squal_avg']}{lift_str}   [{res['diag_n']} samples "
                  f"during roll]")

        notes = ask("Notes (optional)")
        row = {
            "timestamp": datetime.now().isoformat(timespec="seconds"),
            "tester": tester,
            "board_id": board_id,
            "unit_serial": unit_serial,
            "sensor": res["sensor"],
            "ball_color": color,
            "ball_brand": brand,
            "surface": surface,
            "height": height,
            "duration_s": args.duration,
            "motion_pct": res["motion_pct"],
            "path_counts": res.get("path_counts", ""),
            "coherence_pct": res.get("coherence_pct", ""),
            "reversals": res.get("reversals", ""),
            "dir_pairs": res.get("dir_pairs", ""),
            "bins": res["bins"],
            "longest_pause_ms": res["longest_pause_ms"],
            "skew_pct": res.get("skew_pct", ""),
            "verdict": v,
            "notes": notes,
        }
        for k in ("squal_min", "squal_avg", "squal_max",
                  "shutter_min", "shutter_avg", "shutter_max",
                  "pix_min", "pix_avg", "pix_max", "lift_pct", "diag_n",
                  "bin_p10", "bin_med", "bin_max", "slow_bins", "active_bins"):
            row[k] = res.get(k, "")
        append_log(log_path, row)
        counter[0] += 1
        print(f"  logged ({counter[0]} run{'s' if counter[0] != 1 else ''} this session)\n")

        if pinned:
            nxt = ask("Next: [Enter]=rerun this unit, [b]=next sensor "
                      "(unplug, swap, replug), [q]=quit", "")
        else:
            nxt = ask("Next: [Enter]=another ball, [b]=new board, [q]=quit", "")
        if nxt.lower().startswith("q"):
            return "quit"
        if nxt.lower().startswith("b"):
            return "next"


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description="Cyboard wired trackball QC station (PMW3360)")
    ap.add_argument("--port", help="serial port (auto-detected if omitted)")
    ap.add_argument("--log", default="wired-qc-log.csv", help="CSV log file")
    ap.add_argument("--duration", type=int, default=DEFAULT_DURATION,
                    help="seconds of rolling per measurement")
    ap.add_argument("--tester", help="tester name (skips the prompt)")
    ap.add_argument("--ball-color", help="pin the ball color for every run")
    ap.add_argument("--ball-brand", help="pin the ball brand for every run")
    ap.add_argument("--surface", help="pin the ball surface state (clean/dirty/…)")
    ap.add_argument("--height", help="pin the sensor-height string for every run")
    ap.add_argument("--uf2", help=f"QC test firmware to auto-flash "
                    f"(default: {DEFAULT_UF2} in the repo root / this folder)")
    ap.add_argument("--no-flash", action="store_true",
                    help="don't auto-flash; use whatever firmware is on the board")
    ap.add_argument("--reflash", action="store_true",
                    help="flash even if the board already runs the test firmware")
    ap.add_argument("--uf2-drive",
                    help="forced bootloader mount point (if the host won't auto-mount)")
    ap.add_argument("--ship-gate", action="store_true",
                    help="strict customer-flawless verdicts: demote GOOD on any "
                         "slow bin or p10/med < 0.7 (per single run). The "
                         "run-each-ball-twice / reject-if-both-flagged protocol "
                         "is a MANUAL operator procedure — the tool does not "
                         "de-noise across runs.")
    args = ap.parse_args()
    # The firmware clamps the window to [1, 30] s (and falls back to 5 s
    # otherwise); mirror that here so the logged duration_s can't disagree with
    # what the board actually measured.
    if args.duration < 1 or args.duration > 30:
        print(f"!! --duration {args.duration}s out of range; the firmware only "
              f"honors 1–30s. Clamping to 5s.")
        args.duration = 5
    global SHIP_GATE
    SHIP_GATE = args.ship_gate
    if SHIP_GATE:
        print(">> ship-gate ON: strict flawless bar (any slow bin or p10/med < 0.7 demotes)")

    log_path = resolve_log_path(args.log)
    uf2 = None if (args.no_flash or args.port) else find_qc_uf2(args.uf2)
    if not args.no_flash and not args.port and uf2 is None:
        print("!! No QC test firmware (.uf2) found — continuing without auto-flash.")
        print("   Build it with `make cyboard/imprint/tester:qc`, or point at one")
        print("   with --uf2, or flash manually. --no-flash silences this.")

    print("\nCyboard wired trackball QC station (PMW3360)")
    print(f"Log: {os.path.abspath(log_path)}")
    if uf2:
        print(f"Auto-flash: {os.path.basename(uf2)} (same image for either half)")
    print()

    tester = args.tester or ask("Tester name", "maynor")
    counter = [0]
    try:
        while True:
            print("=" * 52)
            board = acquire_board(args, uf2)
            if board is None:
                if ask("Try again with another board? (y/n)", "y").lower().startswith("y"):
                    continue
                break
            ser, sensor_idx, unit_serial = board
            board_id = ask("Board/sensor label (e.g. bt-1; unique serial auto-logged)")
            try:
                outcome = measure_loop(ser, sensor_idx, args, tester, board_id,
                                       unit_serial, log_path, counter)
            finally:
                ser.close()
            if outcome == "quit":
                break
            print("\nUnplug this half and plug in the next one.\n")
    except KeyboardInterrupt:
        print("\ninterrupted.")
    finally:
        print(f"\nDone. {counter[0]} measurement(s) logged to {os.path.abspath(log_path)}")


if __name__ == "__main__":
    main()
