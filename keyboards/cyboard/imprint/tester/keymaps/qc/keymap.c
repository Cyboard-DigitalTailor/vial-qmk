/* Copyright 2026 Cyboard LLC (@Cyboard-DigitalTailor)
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PMW3360 wired-trackball QC firmware.
 *
 * Wired analogue of the ZMK `imprint_qc` build for the PMW3610 (see
 * zmk-pmw3610-driver-private + zmk-environment/factory/imprint/qc-station).
 * It exposes a tiny command shell over a USB CDC virtual serial port
 * (VIRTSER); the host station (qc-station/wired_qc_station.py) drives it.
 *
 * The centrepiece is `qc mon`: a consistency meter. The tester rolls the ball
 * in circles for a few seconds and the firmware bins the reported motion into
 * fixed 25 ms windows, scoring how *smooth* the motion was. This catches the
 * "tracks, but the movement quality is bad" failures that a static SQUAL/
 * shutter snapshot (the old `qc dump` report) misses entirely:
 *
 *   - motion%    fraction of bins that saw any motion (dropouts / stalls)
 *   - pause      longest run of zero-motion bins
 *   - path       total |dx|+|dy| counts over the roll (scale / under-report)
 *   - coherence  per-bin net-over-gross displacement (jitter / thrash)
 *   - reversals  consecutive-bin direction flips (zig-zag)
 *   - dispersion p10/median/max bin magnitude + slow-bin count (micro-stall)
 *
 * PMW3360 extras beyond what the PMW3610 exposed, folded into the rolling
 * surface read: whole-array brightness via Raw_Data_Sum (reported as pix avg)
 * and a lift-detect fraction (lift% — the sensor's own "ball too far / bad
 * seating" bit, sampled across the roll).
 *
 * We drive the sensor via a `custom` pointing-device driver (see rules.mk)
 * whose get_report is our SOLE reader: it reads the raw, pre-HID-clamp 16-bit
 * deltas directly and returns zero cursor movement. This bypasses the int8 HID
 * clamp in pmw33xx_get_report and the drag/snipe transforms in cyboard.c, both
 * of which would destroy the tracking-fidelity signal. POINTING_DEVICE stays
 * enabled so cyboard.c compiles and the pointing task calls our get_report at
 * the ~1 ms POINTING_DEVICE_TASK_THROTTLE_MS tick (our per-poll sample rate).
 * The PMW3360 runs with REST disabled (the common driver writes Config2 = 0x00),
 * so it stays in RUN mode and there is no downshift/false-dropout problem — no
 * force-awake needed.
 *
 * Bench-only. Never shipped to customers.
 */

#include QMK_KEYBOARD_H
#include "virtser.h"
#include "hardware_id.h"
#include "drivers/sensors/pmw33xx_common.h"
#include "drivers/sensors/pmw3360.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The QC jig produces no keystrokes; every key is inert.
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT_tester(
        KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,   KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,
        KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,   KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,
        KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,   KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,
        KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,   KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,
        KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,   KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,
        KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,   KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,
                                    KC_NO, KC_NO, KC_NO, KC_NO,   KC_NO, KC_NO, KC_NO, KC_NO,
                                    KC_NO, KC_NO, KC_NO, KC_NO,   KC_NO, KC_NO, KC_NO, KC_NO
    )
};

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

#define QC_SENSOR          0        // local sensor on the half under test
#define QC_BIN_MS          25       // consistency-meter bin width
// Dispersion history buffer (matches the ZMK reference's 400). At 25 ms/bin
// this covers the first 10 s of a run, so for runs longer than 10 s the
// p10/med/max/slow dispersion stats reflect only the first 10 s (motion%,
// pause, path and coherence still use the whole window). The default run is 5 s.
#define QC_MAX_BINS        400
#define QC_DEFAULT_SECS    5
#define QC_MAX_SECS        30

// `qc dump` static pass/fail thresholds (carried over from the original wired
// tester keymap; bench-calibrate per the README).
#define QC_SQUAL_MIN            75
#define QC_SHUTTER_MAX          8000
#define QC_EXPECTED_PRODUCT_ID  0x42
#define QC_EXPECTED_INV_PID     0xBD

#define QC_ABS(v)   ((v) < 0 ? -(v) : (v))

// ---------------------------------------------------------------------------
// VIRTSER output helpers
// ---------------------------------------------------------------------------

static void qc_puts(const char *s) {
    while (*s) {
        virtser_send((uint8_t)*s++);
    }
}

static void qc_printf(const char *fmt, ...) {
    // Sized for the worst-case consistency line (long run, max field widths)
    // including the trailing CRLF, so the host never sees a truncated line.
    char    buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    if (n >= (int)sizeof(buf)) {
        n = sizeof(buf) - 1;
    }
    for (int i = 0; i < n; i++) {
        virtser_send((uint8_t)buf[i]);
    }
}

// A stable, human-readable name for the sensor on this half. The host's result
// parser captures the "[...]" prefix, so keep the "[name] " shape.
static const char *qc_sensor_name(void) {
    return is_keyboard_left() ? "trackball_left@0" : "trackball_right@0";
}

// ---------------------------------------------------------------------------
// Sensor I/O (single reader; we own the loop)
// ---------------------------------------------------------------------------

// Read the motion register and, if motion is present, the four delta bytes.
// Returns true on motion (and fills *dx/*dy with the raw signed counts since the
// last read); always reports the lift bit in *lifted. Reading REG_Motion latches
// the delta registers, so the four reads below drain that latched value.
static bool qc_read_motion(int16_t *dx, int16_t *dy, bool *lifted) {
    uint8_t mot = pmw33xx_read(QC_SENSOR, REG_Motion);
    *lifted     = (mot & 0x08) != 0;   // Lift_stat
    if (!(mot & 0x80)) {               // MOT
        *dx = 0;
        *dy = 0;
        return false;
    }
    uint8_t xl = pmw33xx_read(QC_SENSOR, REG_Delta_X_L);
    uint8_t xh = pmw33xx_read(QC_SENSOR, REG_Delta_X_H);
    uint8_t yl = pmw33xx_read(QC_SENSOR, REG_Delta_Y_L);
    uint8_t yh = pmw33xx_read(QC_SENSOR, REG_Delta_Y_H);
    *dx        = (int16_t)(((uint16_t)xh << 8) | xl);
    *dy        = (int16_t)(((uint16_t)yh << 8) | yl);
    return true;
}

typedef struct {
    uint8_t  squal;
    uint16_t shutter;
    uint8_t  raw_min;
    uint8_t  raw_sum;   // whole-array brightness proxy (PMW3360-only)
    uint8_t  raw_max;
} qc_surface_t;

static void qc_read_surface(qc_surface_t *s) {
    s->squal        = pmw33xx_read(QC_SENSOR, REG_SQUAL);
    s->raw_sum      = pmw33xx_read(QC_SENSOR, REG_Raw_Data_Sum);
    s->raw_max      = pmw33xx_read(QC_SENSOR, REG_Maximum_Raw_data);
    s->raw_min      = pmw33xx_read(QC_SENSOR, REG_Minimum_Raw_data);
    uint8_t sh_lo   = pmw33xx_read(QC_SENSOR, REG_Shutter_Lower);
    uint8_t sh_hi   = pmw33xx_read(QC_SENSOR, REG_Shutter_Upper);
    s->shutter      = ((uint16_t)sh_hi << 8) | sh_lo;
}

// ---------------------------------------------------------------------------
// Consistency meter (`qc mon`) — mirrors the ZMK pmw3610 `mon` math
// ---------------------------------------------------------------------------

static bool     mon_active = false;
static uint16_t mon_bins_left;
static uint32_t mon_bin_start;   // timer_read32 anchor for the current bin
static uint32_t mon_run_start;   // timer_read32 at window start (skew check)
static uint16_t mon_bins;        // bins sampled
static uint16_t mon_active_bins; // bins with motion
static uint16_t mon_cur_gap, mon_max_gap;

// Continuously-accumulated motion (updated every poll within a bin).
static uint32_t track_path;
static int32_t  track_net_dx, track_net_dy;

// Per-bin snapshots / coherence / reversal state.
static uint32_t mon_last_path;
static int32_t  mon_last_net_dx, mon_last_net_dy;
static uint32_t mon_net_sum;
static int32_t  mon_prev_ndx, mon_prev_ndy;
static uint16_t mon_reversals, mon_dir_pairs;

// Bin-magnitude dispersion history.
static uint16_t mon_bin_counts[QC_MAX_BINS];
static uint16_t mon_bin_n;

// Rolling surface aggregation (sampled every 4th bin while moving).
static uint16_t mon_surf_n;
static uint16_t sq_min, sq_max;  static uint32_t sq_sum;
static uint16_t sh_min, sh_max;  static uint32_t sh_sum;
static uint16_t pix_min, pix_max; static uint32_t pix_sum;  // pix = raw min/sum/max
static uint16_t mon_lift_bins;   // bins where the lift bit was seen
static bool     bin_lift_seen;

static void qc_mon_start(uint16_t secs) {
    if (secs == 0 || secs > QC_MAX_SECS) {
        secs = QC_DEFAULT_SECS;
    }
    // Drain any motion accumulated before the window so bin 0 isn't a spike.
    int16_t dx, dy;
    bool    lifted;
    qc_read_motion(&dx, &dy, &lifted);

    track_path = 0; track_net_dx = 0; track_net_dy = 0;
    mon_last_path = 0; mon_last_net_dx = 0; mon_last_net_dy = 0;
    mon_net_sum = 0;
    mon_prev_ndx = 0; mon_prev_ndy = 0;
    mon_reversals = 0; mon_dir_pairs = 0;
    mon_bins = 0; mon_active_bins = 0;
    mon_cur_gap = 0; mon_max_gap = 0;
    mon_bin_n = 0;
    mon_surf_n = 0;
    sq_min = 0xFFFF; sq_max = 0; sq_sum = 0;
    sh_min = 0xFFFF; sh_max = 0; sh_sum = 0;
    pix_min = 0xFFFF; pix_max = 0; pix_sum = 0;
    mon_lift_bins = 0; bin_lift_seen = false;

    mon_bins_left  = (uint16_t)(secs * (1000u / QC_BIN_MS));
    mon_run_start  = timer_read32();
    mon_bin_start  = mon_run_start;
    mon_active     = true;

    qc_printf("[%s] roll the ball continuously (circles) for %us...\r\n",
              qc_sensor_name(), (unsigned)secs);
}

// One poll: fold the latest motion into the running accumulators.
static void qc_mon_poll(void) {
    int16_t dx, dy;
    bool    lifted;
    bool    moved = qc_read_motion(&dx, &dy, &lifted);
    if (lifted) {
        bin_lift_seen = true;
    }
    if (moved) {
        track_path += (uint32_t)QC_ABS(dx) + (uint32_t)QC_ABS(dy);
        track_net_dx += dx;
        track_net_dy += dy;
    }
}

// Close out one 25 ms bin.
static void qc_mon_bin(void) {
    uint32_t path  = track_path;
    uint32_t delta = path - mon_last_path;
    mon_last_path  = path;

    int32_t ndx = track_net_dx - mon_last_net_dx;
    int32_t ndy = track_net_dy - mon_last_net_dy;
    mon_last_net_dx = track_net_dx;
    mon_last_net_dy = track_net_dy;
    mon_net_sum += (uint32_t)QC_ABS(ndx) + (uint32_t)QC_ABS(ndy);

    if (ndx != 0 || ndy != 0) {
        if (mon_prev_ndx != 0 || mon_prev_ndy != 0) {
            int64_t dot = (int64_t)ndx * mon_prev_ndx + (int64_t)ndy * mon_prev_ndy;
            mon_dir_pairs++;
            if (dot < 0) {
                mon_reversals++;
            }
        }
        mon_prev_ndx = ndx;
        mon_prev_ndy = ndy;
    }

    mon_bins++;
    if (mon_bin_n < QC_MAX_BINS) {
        mon_bin_counts[mon_bin_n++] = delta > 0xFFFFu ? 0xFFFFu : (uint16_t)delta;
    }
    if (delta > 0) {
        mon_active_bins++;
        mon_cur_gap = 0;
    } else {
        mon_cur_gap++;
        if (mon_cur_gap > mon_max_gap) {
            mon_max_gap = mon_cur_gap;
        }
    }
    if (bin_lift_seen) {
        mon_lift_bins++;
    }
    bin_lift_seen = false;

    // Surface bundle every 4th bin (~100 ms) — hand-motion-independent read.
    if ((mon_bins & 0x3u) == 0) {
        qc_surface_t s;
        qc_read_surface(&s);
        mon_surf_n++;
        sq_sum += s.squal;
        if (s.squal < sq_min) sq_min = s.squal;
        if (s.squal > sq_max) sq_max = s.squal;
        sh_sum += s.shutter;
        if (s.shutter < sh_min) sh_min = s.shutter;
        if (s.shutter > sh_max) sh_max = s.shutter;
        pix_sum += s.raw_sum;
        if (s.raw_min < pix_min) pix_min = s.raw_min;
        if (s.raw_max > pix_max) pix_max = s.raw_max;
    }
}

static void qc_mon_finish(void) {
    mon_active = false;
    const char *name = qc_sensor_name();

    uint32_t pct = mon_bins ? ((uint32_t)mon_active_bins * 100u / mon_bins) : 0;

    // Timing-health check: a stretched window (USB/CPU contention) skews the
    // result. Compare actual elapsed to the expected window.
    int32_t elapsed  = (int32_t)timer_elapsed32(mon_run_start);
    int32_t expected = (int32_t)mon_bins * QC_BIN_MS;
    int32_t skew_pct = expected ? ((elapsed - expected) * 100 / expected) : 0;

    uint32_t total_counts = track_path;
    uint32_t coh_pct = total_counts ? (mon_net_sum * 100u / total_counts) : 0;
    if (coh_pct > 100u) {
        coh_pct = 100u;
    }

    // Dispersion: compact active bins, insertion sort, p10/median/max + slow
    // bins (active bins under 1/4 of the run's own median).
    uint16_t n_act = 0;
    for (uint16_t i = 0; i < mon_bin_n; i++) {
        if (mon_bin_counts[i] > 0) {
            mon_bin_counts[n_act++] = mon_bin_counts[i];
        }
    }
    for (uint16_t i = 1; i < n_act; i++) {
        uint16_t v = mon_bin_counts[i];
        int32_t  j = (int32_t)i - 1;
        while (j >= 0 && mon_bin_counts[j] > v) {
            mon_bin_counts[j + 1] = mon_bin_counts[j];
            j--;
        }
        mon_bin_counts[j + 1] = v;
    }
    uint16_t bin_p10 = n_act ? mon_bin_counts[n_act / 10] : 0;
    uint16_t bin_med = n_act ? mon_bin_counts[n_act / 2] : 0;
    uint16_t bin_max = n_act ? mon_bin_counts[n_act - 1] : 0;
    uint16_t slow    = 0;
    while (slow < n_act && (uint32_t)mon_bin_counts[slow] * 4u < bin_med) {
        slow++;
    }

    qc_printf("[%s] consistency: motion in %u%% of %u x %ums bins; longest pause %ums; "
              "path %u counts; coherence %u%%; reversals %u/%u; "
              "bins p10/med/max %u/%u/%u; slow %u/%u\r\n",
              name, (unsigned)pct, (unsigned)mon_bins, QC_BIN_MS,
              (unsigned)(mon_max_gap * QC_BIN_MS), (unsigned)total_counts,
              (unsigned)coh_pct, (unsigned)mon_reversals, (unsigned)mon_dir_pairs,
              (unsigned)bin_p10, (unsigned)bin_med, (unsigned)bin_max,
              (unsigned)slow, (unsigned)n_act);

    if (skew_pct > 10) {
        qc_printf("[%s] timing skew +%d%% (window ran %dms vs %dms expected) — "
                  "result unreliable, redo with the terminal idle\r\n",
                  name, (int)skew_pct, (int)elapsed, (int)expected);
    }

    if (mon_surf_n > 0) {
        uint32_t n = mon_surf_n;
        uint32_t lift_pct = mon_bins ? ((uint32_t)mon_lift_bins * 100u / mon_bins) : 0;
        qc_printf("[%s] surface(rolling): squal %u/%u/%u shutter %u/%u/%u "
                  "pix %u/%u/%u lift %u%% (%u samples)\r\n",
                  name,
                  (unsigned)sq_min, (unsigned)(sq_sum / n), (unsigned)sq_max,
                  (unsigned)sh_min, (unsigned)(sh_sum / n), (unsigned)sh_max,
                  (unsigned)pix_min, (unsigned)(pix_sum / n), (unsigned)pix_max,
                  (unsigned)lift_pct, (unsigned)n);
    }
}

// ---------------------------------------------------------------------------
// Static register report (`qc dump`) — the old snapshot, over VIRTSER
// ---------------------------------------------------------------------------

static void qc_dump(void) {
    const char *name = qc_sensor_name();
    bool        pass = true;

    uint8_t pid  = pmw33xx_read(QC_SENSOR, REG_Product_ID);
    uint8_t ipid = pmw33xx_read(QC_SENSOR, REG_Inverse_Product_ID);
    uint8_t rev  = pmw33xx_read(QC_SENSOR, REG_Revision_ID);
    uint8_t srom = pmw33xx_read(QC_SENSOR, REG_SROM_ID);
    if (pid != QC_EXPECTED_PRODUCT_ID || ipid != QC_EXPECTED_INV_PID) {
        pass = false;
    }

    // SQUAL averaged over 10 samples (trigger a motion read to refresh each).
    uint16_t sqsum = 0;
    uint8_t  sqmin = 255, sqmax = 0;
    for (int i = 0; i < 10; i++) {
        pmw33xx_read(QC_SENSOR, REG_Motion);
        wait_ms(10);
        uint8_t sq = pmw33xx_read(QC_SENSOR, REG_SQUAL);
        sqsum += sq;
        if (sq < sqmin) sqmin = sq;
        if (sq > sqmax) sqmax = sq;
    }
    uint8_t sqavg = (uint8_t)(sqsum / 10);
    if (sqavg < QC_SQUAL_MIN) {
        pass = false;
    }

    pmw33xx_read(QC_SENSOR, REG_Motion);
    wait_ms(1);
    qc_surface_t s;
    qc_read_surface(&s);
    if (s.shutter > QC_SHUTTER_MAX) {
        pass = false;
    }

    qc_printf("[%s] dump: product_id=0x%02X (expect 0x%02X) inv=0x%02X (expect 0x%02X) "
              "rev=0x%02X srom=0x%02X\r\n",
              name, pid, QC_EXPECTED_PRODUCT_ID, ipid, QC_EXPECTED_INV_PID, rev, srom);
    qc_printf("[%s] dump: squal avg/min/max=%u/%u/%u (min %u) shutter=%u (max %u) "
              "raw min/sum/max=%u/%u/%u cpi=%u\r\n",
              name, sqavg, sqmin, sqmax, QC_SQUAL_MIN, s.shutter, QC_SHUTTER_MAX,
              s.raw_min, s.raw_sum, s.raw_max, pmw33xx_get_cpi(QC_SENSOR));
    qc_printf("[%s] dump: result %s\r\n", name, pass ? "PASS" : "FAIL");
}

static void qc_print_id(void) {
    hardware_id_t   id  = get_hardware_id();
    const uint8_t  *b   = (const uint8_t *)&id;
    char            hex[2 * 8 + 1];
    // RP2040's unique id is 8 bytes (fills data[0..1]); print those.
    for (int i = 0; i < 8; i++) {
        snprintf(hex + i * 2, 3, "%02x", b[i]);
    }
    hex[16] = '\0';
    qc_printf("unit_serial=%s (RP2040 flash uid; the PMW3360 has no unique serial)\r\n", hex);
}

// ---------------------------------------------------------------------------
// Command shell over VIRTSER
// ---------------------------------------------------------------------------

static char    rx_buf[64];
static uint8_t rx_len   = 0;
static char    cmd_line[64];
static bool    cmd_ready = false;

// Called from virtser_task() in the main protocol loop (same context as
// housekeeping) — safe to buffer here; we execute from housekeeping so a long
// `mon` run never blocks the USB RX drain.
void virtser_recv(const uint8_t ch) {
    if (ch == '\r' || ch == '\n') {
        if (rx_len > 0 && !cmd_ready) {
            memcpy(cmd_line, rx_buf, rx_len);
            cmd_line[rx_len] = '\0';
            cmd_ready        = true;
        }
        rx_len = 0;
    } else if (rx_len < sizeof(rx_buf) - 1) {
        rx_buf[rx_len++] = (char)ch;
    }
}

static void qc_help(void) {
    const char *name = qc_sensor_name();
    qc_printf("[%s] PMW3360 QC shell. commands:\r\n", name);
    qc_puts("  qc list            list the sensor and its index\r\n");
    qc_puts("  qc mon [seconds]   consistency meter (default 5s): roll circles\r\n");
    qc_puts("  qc dump            static register report (id/squal/shutter/raw/cpi)\r\n");
    qc_puts("  qc cpi [value]     get, or set (100-12000, step 100), the CPI\r\n");
    qc_puts("  qc id              print the unit's unique serial (RP2040 uid)\r\n");
    qc_puts("  qc boot            reboot into the UF2 bootloader for re-flashing\r\n");
}

// Accepts commands with or without a leading "qc " (or legacy "pmw3360 ").
static void qc_dispatch(char *line) {
    // Skip an optional command-group token so both `mon 5` and `qc mon 5` work.
    char *p = line;
    while (*p == ' ') p++;
    if (strncmp(p, "qc ", 3) == 0) {
        p += 3;
    } else if (strncmp(p, "pmw3360 ", 8) == 0) {
        p += 8;
    } else if (strncmp(p, "pmw3610 ", 8) == 0) {  // tolerate the wireless tool's prefix
        p += 8;
    }
    while (*p == ' ') p++;

    // Split verb / argument.
    char *verb = p;
    char *arg  = strchr(p, ' ');
    if (arg) {
        *arg++ = '\0';
        while (*arg == ' ') arg++;
    }

    if (strcmp(verb, "list") == 0) {
        qc_printf("0: %s\r\n", qc_sensor_name());
    } else if (strcmp(verb, "mon") == 0) {
        if (mon_active) {
            qc_printf("[%s] a measurement is already running\r\n", qc_sensor_name());
        } else {
            uint16_t secs = (arg && *arg) ? (uint16_t)atoi(arg) : QC_DEFAULT_SECS;
            qc_mon_start(secs);
        }
    } else if (strcmp(verb, "dump") == 0) {
        // dump drains REG_Motion and blocks ~100ms; both would corrupt a live
        // mon run, so refuse while one is in flight.
        if (mon_active) {
            qc_printf("[%s] a measurement is running; try dump after it\r\n",
                      qc_sensor_name());
        } else {
            qc_dump();
        }
    } else if (strcmp(verb, "cpi") == 0) {
        // Changing CPI mid-run would rescale path counts, so refuse a set while
        // a mon is running; a bare `cpi` read is harmless.
        if (arg && *arg && mon_active) {
            qc_printf("[%s] a measurement is running; set cpi after it\r\n",
                      qc_sensor_name());
        } else if (arg && *arg) {
            pmw33xx_set_cpi(QC_SENSOR, (uint16_t)atoi(arg));
        }
        qc_printf("[%s] cpi=%u\r\n", qc_sensor_name(), pmw33xx_get_cpi(QC_SENSOR));
    } else if (strcmp(verb, "id") == 0) {
        qc_print_id();
    } else if (strcmp(verb, "boot") == 0 || strcmp(verb, "dfu") == 0 ||
               strcmp(verb, "bootloader") == 0) {
        qc_puts("rebooting into the UF2 bootloader for re-flash...\r\n");
        wait_ms(50);
        reset_keyboard();     // bootloader_jump() -> RPI-RP2
    } else if (strcmp(verb, "help") == 0 || verb[0] == '\0') {
        qc_help();
    } else {
        qc_printf("[%s] unknown command '%s' (try: qc help)\r\n", qc_sensor_name(), verb);
    }
}

// ---------------------------------------------------------------------------
// Custom pointing-device driver — our sole sensor reader
// ---------------------------------------------------------------------------
// POINTING_DEVICE_DRIVER = custom, so these four weak hooks (declared in
// pointing_device.c) are ours. The pointing task calls get_report every ~1 ms
// on the master (the half plugged into USB for QC).

void pointing_device_driver_init(void) {
    pmw33xx_init(QC_SENSOR);
}

report_mouse_t pointing_device_driver_get_report(report_mouse_t mouse_report) {
    if (mon_active) {
        qc_mon_poll();   // fold this tick's raw motion into the accumulators
    }
    // QC jig: never move the cursor.
    mouse_report.x = 0;
    mouse_report.y = 0;
    return mouse_report;
}

uint16_t pointing_device_driver_get_cpi(void) {
    return pmw33xx_get_cpi(QC_SENSOR);
}

void pointing_device_driver_set_cpi(uint16_t cpi) {
    pmw33xx_set_cpi(QC_SENSOR, cpi);
}

// ---------------------------------------------------------------------------
// QMK main-loop hook: command dispatch + consistency-meter bin timing
// ---------------------------------------------------------------------------

void housekeeping_task_user(void) {
    if (cmd_ready) {
        cmd_ready = false;
        qc_dispatch(cmd_line);
    }

    // Motion is sampled in get_report (~1 ms); here we only close out each
    // 25 ms bin and finish the run. Both run in the main loop, so the shared
    // accumulators need no locking.
    if (mon_active && timer_elapsed32(mon_bin_start) >= QC_BIN_MS) {
        mon_bin_start += QC_BIN_MS;
        qc_mon_bin();
        if (--mon_bins_left == 0) {
            qc_mon_finish();
        }
    }
}
