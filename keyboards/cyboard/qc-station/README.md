# Wired trackball QC station (PMW3360)

A command-line tool for checking how well a **wired** Cyboard trackball tracks.
It's the wired counterpart of the wireless
`zmk-environment/factory/imprint/qc-station` (which tests the PMW3610 over ZMK).
This one drives the PMW3360 QC test firmware over USB serial: you roll the ball
in circles for a few seconds and it scores how *smooth* the motion was, then
logs every run to a CSV.

The wired PMW3360 has generally tracked much more consistently than the wireless
PMW3610 — this gives us the same objective GOOD/MARGINAL/FAIL verdict and a data
trail to prove a unit passed, rather than relying on feel.

## What it measures

The core is a **consistency meter** (`qc mon`) — identical math to the wireless
station, so runs are directly comparable across the two sensors:

- **motion %** — fraction of the 25 ms bins that saw any motion. Higher = fewer
  dropouts. A good ball is ~97–100%.
- **path counts** — total reported motion magnitude (|dx|+|dy|) over the roll.
  Catches a ball that *follows but at reduced scale* (high motion% yet far fewer
  counts). Compare to a known-good black-ball baseline **on the same unit** at a
  similar roll speed — it's a relative check, not an absolute threshold.
- **longest pause** — the longest stretch with no motion. Short is good.
- **coherence %** — per-bin net-over-gross displacement: ~100% = smooth,
  direction-consistent tracking; low = jitter/thrash ("follows the finger but
  feels bad") even when motion% is high.
- **reversals** `N/M` — consecutive-bin direction flips out of M moving-bin
  comparisons. Catches slow "zig-zag" that coherence is blind to. Smooth circles
  ≈ 0.
- **dispersion** — `bins p10/med/max` and `slow s/n`: per-bin magnitude
  percentiles and the count of active bins under ¼ of the run's median. Catches
  intermittent slowdowns / micro-stalls that motion%/pause/coherence all miss.

### PMW3360 surface reads (sampled during the roll, motion-independent)

- **squal** min/avg/max — surface quality (trackable features). Higher = better.
- **shutter** min/avg/max — exposure time. Rises on a less-reflective surface.
- **pix** min/avg/max — raw pixel brightness. `pix avg` is the PMW3360
  **Raw_Data_Sum** register (whole-array brightness), a read the PMW3610 didn't
  expose. Dimmer = worse surface.
- **lift %** — fraction of bins where the sensor asserted its **lift-detect**
  bit. A well-seated ball at the right height reads ~0%; a high lift% points at
  the ball sitting too far from the lens / a bad mount — a hardware signal
  independent of how you rolled.

`shutter↑ / pix↓ / lift↑` together are the "worse surface or seating" signature,
and they're independent of hand motion, so they separate good balls from bad
even when `squal` doesn't.

## What you need

- A wired Cyboard half and a USB-C cable to the half's own computer port (not the
  split-link port).
- **You don't have to flash it first** — the tool auto-flashes the QC firmware
  when you plug in (see "Auto-flash"). The firmware
  (`cyboard_imprint_tester_qc.uf2`) is built from this repo and is bench-only;
  it is never shipped to customers.
- Python 3 with `pyserial` (+ `udisksctl` to auto-mount the bootloader drive) —
  provided by the repo-root `nix-shell`, or the `shell.nix` in this folder.

## Build the firmware

From the vial-qmk repo root, in the Nix shell:

```
nix-shell --run "make cyboard/imprint/tester:qc"
```

This writes `cyboard_imprint_tester_qc.uf2` to the repo root. One image works on
either half — it only talks to that half's local trackball over USB.

> This VM has no PMW3360 hardware, so the firmware here has been **compiled** but
> not run on a sensor. Validate on the bench and calibrate the thresholds (below)
> before trusting the verdicts. In particular, on the first bench run confirm the
> **`pix` (Raw_Data_Sum / min / max) and `squal` values are non-zero and move**
> when you change the surface — i.e. that the PMW3360 refreshes registers
> 0x07–0x0c during normal RUN-mode tracking without a separate frame-capture
> grab. (`pix avg` here is Raw_Data_Sum, the whole-array brightness, so these
> values are **not** directly comparable to the wireless PMW3610 `pix` column.)

## Run a QC session

From the repo root (so the tool finds the freshly built `.uf2`):

```
nix-shell --run "python3 keyboards/cyboard/qc-station/wired_qc_station.py"
```

The tool will:

1. Ask for the **tester name** once.
2. **Auto-flash** the plugged-in half (double-tap RESET if it asks), find its
   serial port, and confirm the sensor.
3. Ask for a **board/unit ID**.
4. For each measurement ask **ball color**, **brand**, **surface**
   (clean/dirty), and **sensor height**.
5. Show **"ROLL THE BALL IN CIRCLES"** with a countdown — keep the ball moving
   the whole time.
6. Print a verdict and log the result.
7. Offer another ball, `b` = next board, or `q` = quit.

Suggested routine per unit: run a **known-good black ball** first as the
baseline, then the suspect ball on the same unit, and compare motion% **and**
path counts.

### Clean vs. dirty

Run a suspect ball **clean**, then handle it and run it **dirty**:
`bad clean → good dirty` = surface/optical (glossy ball throws too few features);
`bad both ways` = electrical / height / mount. Logging the pair makes it clear.

## Auto-flash

On start (and each `b` = new board) the tool:

1. Asks the board to enter its RP2040 UF2 bootloader (`qc boot`). If the current
   firmware doesn't support it, it prints **"Double-tap the RESET button now"**.
2. Mounts the `RPI-RP2` drive, copies the `.uf2`, waits for reboot, and tests.

Skip with `--no-flash`; force with `--reflash`; point at a specific image with
`--uf2 path.uf2`.

## The log

Rows append to `wired-qc-log.csv` (override with `--log`). Columns: timestamp,
tester, board_id, **unit_serial** (RP2040 flash UID, read automatically), sensor,
ball_color, ball_brand, surface, height, duration_s, motion_pct, path_counts,
coherence_pct, reversals, dir_pairs, bin_p10/med/max, slow_bins, active_bins,
bins, longest_pause_ms, skew_pct, squal_*, shutter_*, pix_* (pix_avg =
Raw_Data_Sum), **lift_pct**, diag_n, verdict, notes.

- **unit_serial** — the PMW3360 has no unique serial (fixed product id 0x42 +
  inverse 0xBD on every chip), so this is the RP2040's flash UID, identifying the
  *controller/half*. Give each board a short label too (`bt-1`…) for readability.

## Options

```
--port /dev/ttyACM0     name the port yourself
--log units.csv         choose the log file
--duration 8            longer rolling window (default 5s)
--tester maynor         skip the tester prompt
--no-flash              use the current firmware, don't auto-flash
--reflash               flash even if the QC firmware is present
--uf2 path.uf2          flash a specific image
--ship-gate             strict customer-flawless verdicts (see below)
--ball-color / --ball-brand / --surface / --height   pin metadata (campaign mode)
```

## Calibrating the verdict

The thresholds in `wired_qc_station.py` (`GOOD_PCT`, `PAUSE_FLAG_MS`,
`SLOW_FRAC_FLAG`, `P10_MED_FLAG`, and the `--ship-gate` bars) are **seeded from
the wireless PMW3610 calibration** and are a conservative starting point. Because
the wired sensor is more consistent, recalibrate against in-hand feel:

1. Collect runs across known-good and known-bad wired units (include one
   deliberately **slow** roll per unit — the dispersion defect is speed-
   dependent).
2. Find the motion%/pause/dispersion cut that matches "feels shippable".
3. Update the constants and note the calibration date + basis in a comment, the
   way the wireless station's thresholds are annotated.

## Manual poking (serial terminal)

The firmware also answers a small shell over the same port if you want to poke it
by hand (`qc help`): `qc list`, `qc mon [seconds]`, `qc dump` (static
id/squal/shutter/raw/cpi report), `qc cpi [value]`, `qc id`, `qc boot`.
`qc mon` is what this tool automates.
