// Copyright 2021 cyboard (@cyboard)
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

// Defines names for use in layer keycodes and the keymap
//enum layer_names {
//    BASE,  // default layer
//};

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {

    [0] = LAYOUT_abhijit_patel(
        KC_TAB,  KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,                                                KC_Y,    KC_U,    KC_I,    KC_O,    KC_P,    KC_BSPC,
        KC_LCTL, KC_A,    KC_S,    KC_D,    KC_F,    KC_G,                                                KC_H,    KC_J,    KC_K,    KC_L,    KC_SCLN, KC_QUOT,
        KC_LSFT, KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,    KC_F1,   KC_F2,           KC_F3,   KC_F4,   KC_N,    KC_M,    KC_COMM, KC_DOT,  KC_SLSH, KC_ESC,
                                            KC_LGUI, KC_LALT, KC_ENT,  MO(1),           MO(1),   KC_SPC,  MO(2),   MO(3)
    ),

    [1] = LAYOUT_abhijit_patel(
        _______, _______, KC_BSLS, KC_SLSH, _______, _______,                                             KC_EQL,  KC_AMPR, KC_LPRN, KC_RPRN, KC_MINS, KC_DEL,
        _______, KC_EXLM, KC_AT,   KC_HASH, _______, KC_CIRC,                                             _______, KC_QUOT, KC_LBRC, KC_RBRC, _______, _______,
        _______, _______, _______, _______, _______, KC_GRV,  _______, _______,         _______, _______, _______, KC_PIPE, KC_LCBR, KC_RCBR, _______, _______,
                                            _______, _______, _______, _______,         _______, _______, _______, _______
    ),

    [2] = LAYOUT_abhijit_patel(
        _______, _______, _______, _______, KC_DLR,  KC_PERC,                                             KC_EQL,  KC_7,    KC_8,    KC_9,    _______, _______,
        _______, _______, _______, _______, _______, _______,                                             KC_ASTR, KC_4,    KC_5,    KC_6,    _______, _______,
        _______, _______, _______, _______, _______, _______, _______, _______,         _______, _______, KC_PLUS, KC_1,    KC_2,    KC_3,    KC_DOT,  _______,
                                            _______, _______, _______, _______,         _______, _______, _______, KC_0
    ),
    
    [3] = LAYOUT_abhijit_patel(
        _______, _______, _______, _______, _______, _______,                                             _______, KC_HOME, KC_UP,   KC_END,  _______, _______,
        _______, _______, _______, _______, _______, _______,                                             _______, KC_LEFT, KC_DOWN, KC_RGHT, _______, _______,
        _______, _______, _______, _______, _______, _______, _______, _______,         _______, _______, _______, _______, _______, _______, _______, _______,
                                            _______, _______, _______, _______,         _______, _______, _______, _______
    ),
    
    [4] = LAYOUT_abhijit_patel(
        _______, _______, _______, _______, _______, _______,                                             _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______,                                             _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______, _______, _______,         _______, _______, _______, _______, _______, _______, _______, _______,
                                            _______, _______, _______, _______,         _______, _______, _______, _______
    ),
    
    [5] = LAYOUT_abhijit_patel(
        _______, _______, _______, _______, _______, _______,                                             _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______,                                             _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______, _______, _______,         _______, _______, _______, _______, _______, _______, _______, _______,
                                            _______, _______, _______, _______,         _______, _______, _______, _______
    )
};
