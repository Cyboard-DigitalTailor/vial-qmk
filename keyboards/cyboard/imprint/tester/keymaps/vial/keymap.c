/* Copyright 2023 Cyboard LLC (@Cyboard-DigitalTailor)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include QMK_KEYBOARD_H
#include "print.h"
#include "drivers/sensors/pmw33xx_common.h"
#include "drivers/sensors/pmw3360.h"

// QC thresholds
#define QC_SQUAL_MIN 75
#define QC_SHUTTER_MAX 8000
#define QC_EXPECTED_PRODUCT_ID 0x42
#define QC_EXPECTED_INV_PRODUCT_ID 0xBD

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {

    [0] = LAYOUT_tester(
        KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC,         KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC,
        KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC,         KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC,
        KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC,         KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC,
        KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC,         KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC,
        KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC,         KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC,
        KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC,         KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC, KC_SPC,
                                        KC_SPC, KC_SPC, KC_SPC, KC_SPC,         KC_SPC, KC_SPC, KC_SPC, KC_SPC,
                                        KC_SPC, KC_SPC, KC_SPC, KC_SPC,         KC_SPC, KC_SPC, KC_SPC, KC_SPC
    ),

    [1] = LAYOUT_tester(
        _______, _______, _______, _______, _______, _______, _______, _______,         _______, _______, _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______, _______, _______,         _______, _______, _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______, _______, _______,         _______, _______, _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______, _______, _______,         _______, _______, _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______, _______, _______,         _______, _______, _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______, _______, _______,         _______, _______, _______, _______, _______, _______, _______, _______,
                                            _______, _______, _______, _______,         _______, _______, _______, _______,
                                            _______, _______, _______, _______,         _______, _______, _______, _______
    )
};

// Run PMW3360 QC diagnostics
void pmw3360_qc_report(uint8_t sensor) {
    bool pass = true;

    print("\n=== PMW3360 QC Report ===\n");
    uprintf("Sensor: %u\n", sensor);

    // Product ID check
    uint8_t product_id = pmw33xx_read(sensor, REG_Product_ID);
    uint8_t inv_product_id = pmw33xx_read(sensor, REG_Inverse_Product_ID);
    uprintf("Product ID: 0x%02X (expect 0x%02X) %s\n",
            product_id, QC_EXPECTED_PRODUCT_ID,
            (product_id == QC_EXPECTED_PRODUCT_ID) ? "OK" : "FAIL");
    uprintf("Inverse ID: 0x%02X (expect 0x%02X) %s\n",
            inv_product_id, QC_EXPECTED_INV_PRODUCT_ID,
            (inv_product_id == QC_EXPECTED_INV_PRODUCT_ID) ? "OK" : "FAIL");
    if (product_id != QC_EXPECTED_PRODUCT_ID || inv_product_id != QC_EXPECTED_INV_PRODUCT_ID) {
        pass = false;
    }

    // SROM (firmware) ID
    // Note: SROM ID 0x00 is normal if firmware upload is not enabled
    // The PMW3360 will run on internal ROM which is sufficient for basic operation
    uint8_t srom_id = pmw33xx_read(sensor, REG_SROM_ID);
    uprintf("SROM ID: 0x%02X %s\n", srom_id,
            (srom_id == 0x00) ? "(no external firmware - OK)" : "");
    // Don't fail on SROM 0x00 - it's expected without firmware upload

    // SQUAL - read multiple samples for average
    uint16_t squal_sum = 0;
    uint8_t squal_min = 255;
    uint8_t squal_max = 0;
    for (int i = 0; i < 10; i++) {
        // Need to trigger a motion read first to update SQUAL
        pmw33xx_read(sensor, REG_Motion);
        wait_ms(10);
        uint8_t squal = pmw33xx_read(sensor, REG_SQUAL);
        squal_sum += squal;
        if (squal < squal_min) squal_min = squal;
        if (squal > squal_max) squal_max = squal;
    }
    uint8_t squal_avg = squal_sum / 10;
    uprintf("SQUAL: avg=%u min=%u max=%u (min threshold: %u) %s\n",
            squal_avg, squal_min, squal_max, QC_SQUAL_MIN,
            (squal_avg >= QC_SQUAL_MIN) ? "OK" : "FAIL");
    if (squal_avg < QC_SQUAL_MIN) {
        pass = false;
    }

    // Shutter (exposure time)
    pmw33xx_read(sensor, REG_Motion); // trigger update
    wait_ms(1);
    uint16_t shutter = (pmw33xx_read(sensor, REG_Shutter_Upper) << 8) |
                        pmw33xx_read(sensor, REG_Shutter_Lower);
    uprintf("Shutter: %u (max threshold: %u) %s\n",
            shutter, QC_SHUTTER_MAX,
            (shutter < QC_SHUTTER_MAX) ? "OK" : "WARN");

    // Raw data sum (pixel brightness)
    uint8_t raw_sum = pmw33xx_read(sensor, REG_Raw_Data_Sum);
    uprintf("Raw Data Sum: %u\n", raw_sum);

    // Min/Max raw data
    uint8_t raw_max = pmw33xx_read(sensor, REG_Maximum_Raw_data);
    uint8_t raw_min = pmw33xx_read(sensor, REG_Minimum_Raw_data);
    uprintf("Raw Data Range: %u - %u\n", raw_min, raw_max);

    // CPI setting
    uint16_t cpi = pmw33xx_get_cpi(sensor);
    uprintf("CPI: %u\n", cpi);

    // Overall result
    print("-------------------------\n");
    uprintf("QC Result: %s\n", pass ? "PASS" : "FAIL");
    print("=========================\n\n");
}

void keyboard_post_init_user(void) {
    // Wait for USB/console to be ready
    wait_ms(1000);

    // Run QC report on startup
    print("\n\n*** Cyboard PMW3360 QC Tester ***\n");
    print("Press any key to run QC report again\n\n");

    pmw3360_qc_report(0);
}

// Run QC report on any keypress
bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (record->event.pressed) {
        pmw3360_qc_report(0);
    }
    return true;
}

// Combine pointing device reports (standard behavior)
report_mouse_t pointing_device_task_combined_user(report_mouse_t left_report, report_mouse_t right_report) {
    left_report.x += right_report.x;
    left_report.y += right_report.y;
    left_report.h += right_report.h;
    left_report.v += right_report.v;
    left_report.buttons |= right_report.buttons;
    return left_report;
}

/* ===== BEGIN RGB LED index reference (auto-generated) =====
 *
 * Per-key RGB LED indices for this board, generated from
 * keyboards/cyboard/imprint/tester/info.json (rgb_matrix.layout).
 * Each number is the global LED index of the key at that physical
 * position in LAYOUT_tester; '--' means that key has no per-key LED.
 * Split halves: left half = LED indices 0-55, right half = LED indices 56-111 (rgb_matrix.split_count [56, 56]).
 * Use the index with rgb_matrix_set_color(index, r, g, b).
 *
 *         45   39   33   27   21   15    9    3      59   65   71   77   83   89   95  101
 *         46   40   34   28   22   16   10    4      60   66   72   78   84   90   96  102
 *         47   41   35   29   23   17   11    5      61   67   73   79   85   91   97  103
 *         42   36   30   24   18   12    6    0      56   62   68   74   80   86   92   98
 *         43   37   31   25   19   13    7    1      57   63   69   75   81   87   93   99
 *         44   38   32   26   20   14    8    2      48   49   50   51  107  106  105  104
 *         58   64   70   76      82   88   94  100
 *         52   53   54   55     111  110  109  108
 *
 * Example - light one key (e.g. Caps Lock) red while the lock is on:
 *
 *   #define CAPS_LED_INDEX 5 // Replace with your key's LED index above
 *
 *   bool rgb_matrix_indicators_user(void) {
 *       rgb_matrix_set_color(
 *           CAPS_LED_INDEX,
 *           host_keyboard_led_state().caps_lock ? 255 : 0,
 *           0,
 *           0
 *       );
 *       return true;
 *   }
 *
 * ===== END RGB LED index reference ===== */
