/* Copyright 2023 Cyboard LLC (@Cyboard-DigitalTailor)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include QMK_KEYBOARD_H

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
