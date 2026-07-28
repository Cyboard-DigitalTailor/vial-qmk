# PMW3360 wired-trackball QC firmware — bench/QC use only, never shipped.
#
# Build:  make cyboard/imprint/tester:qc
#         (or: qmk compile -kb cyboard/imprint/tester -km qc)
# Flash:  double-tap RESET on the half under test -> RPI-RP2 drive -> drop the
#         cyboard_imprint_tester_qc.uf2. One image works on either half.
#
# This is the wired analogue of the ZMK `imprint_qc` build for the PMW3610. It
# exposes a small command shell over a USB CDC virtual serial port (VIRTSER)
# that the host QC station drives: `qc mon`, `qc dump`, `qc id`, ...
# See keymap.c and ../../../qc-station/ for the host tool.

# Replace the stock pmw3360 pointing driver with a `custom` one whose get_report
# is OUR sole sensor reader: it reads the raw, unclamped 16-bit deltas directly
# (bypassing the int8 HID clamp in pmw33xx_get_report and the Charybdis
# drag/snipe transforms in cyboard.c) and returns zero cursor movement, so the
# QC jig never moves the pointer. POINTING_DEVICE stays enabled (keyboard-level)
# so cyboard.c still compiles and the pointing task drives our reader at the
# ~1 ms POINTING_DEVICE_TASK_THROTTLE_MS tick.
POINTING_DEVICE_DRIVER = custom
SPI_DRIVER_REQUIRED = yes
SRC += drivers/sensors/pmw33xx_common.c
SRC += drivers/sensors/pmw3360.c
OPT_DEFS += -DPOINTING_DEVICE_DRIVER_pmw3360

# Bidirectional USB CDC serial for the QC command shell + result stream.
VIRTSER_ENABLE = yes
# CONSOLE (raw-HID debug) is on at the keyboard level; we don't use it here, so
# drop it. All QC output goes over VIRTSER.
CONSOLE_ENABLE = no

# Bare bench jig: no VIA/Vial, no extra keycodes.
VIA_ENABLE = no
VIAL_ENABLE = no
RAW_ENABLE = no

LTO_ENABLE = yes
