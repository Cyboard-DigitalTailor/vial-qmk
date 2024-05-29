/*
 * Copyright 2020 Christopher Courtney <drashna@live.com> (@drashna)
 * Copyright 2021 Quentin LEBASTARD <qlebastard@gmail.com>
 * Copyright 2021 Charly Delay <charly@codesink.dev> (@0xcharly)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Publicw License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "cyboard.h"
#include "transactions.h"
#include <string.h>

#ifdef CONSOLE_ENABLE
#    include "print.h"
#endif // CONSOLE_ENABLE

#ifdef SPLIT_POINTING_ENABLE
#    ifndef CHARYBDIS_MINIMUM_DEFAULT_DPI
#        define CHARYBDIS_MINIMUM_DEFAULT_DPI 400
#    endif // CHARYBDIS_MINIMUM_DEFAULT_DPI

#    ifndef CHARYBDIS_DEFAULT_DPI_CONFIG_STEP
#        define CHARYBDIS_DEFAULT_DPI_CONFIG_STEP 200
#    endif // CHARYBDIS_DEFAULT_DPI_CONFIG_STEP

#    ifndef CHARYBDIS_MINIMUM_SNIPING_DPI
#        define CHARYBDIS_MINIMUM_SNIPING_DPI 200
#    endif // CHARYBDIS_MINIMUM_SNIPER_MODE_DPI

#    ifndef CHARYBDIS_SNIPING_DPI_CONFIG_STEP
#        define CHARYBDIS_SNIPING_DPI_CONFIG_STEP 100
#    endif // CHARYBDIS_SNIPING_DPI_CONFIG_STEP

// Fixed DPI for drag-scroll.
#    ifndef CHARYBDIS_DRAGSCROLL_DPI
#        define CHARYBDIS_DRAGSCROLL_DPI 100
#    endif // CHARYBDIS_DRAGSCROLL_DPI

#    ifndef CHARYBDIS_DRAGSCROLL_BUFFER_SIZE
#        define CHARYBDIS_DRAGSCROLL_BUFFER_SIZE 6
#    endif // !CHARYBDIS_DRAGSCROLL_BUFFER_SIZE

typedef union {
    uint8_t raw;
    struct {
        uint8_t pointer_default_dpi : 4; // 16 steps available.
        uint8_t pointer_sniping_dpi : 2; // 4 steps available.
        bool    is_dragscroll_enabled : 1;
        bool    is_sniping_enabled : 1;
    } __attribute__((packed));
} charybdis_config_t;

static charybdis_config_t g_charybdis_config = {0};
static charybdis_config_t g_charybdis_config_left = {0};
static charybdis_config_t g_charybdis_config_right = {0};


/**
 * \brief Set the value of `config` from EEPROM.
 *
 * Note that `is_dragscroll_enabled` and `is_sniping_enabled` are purposefully
 * ignored since we do not want to persist this state to memory.  In practice,
 * this state is always written to maximize write-performances.  Therefore, we
 * explicitly set them to `false` in this function.
 */
static void read_charybdis_config_from_eeprom(charybdis_config_t* config) {
    config->raw                   = eeconfig_read_kb() & 0xff;
    config->is_dragscroll_enabled = false;
    config->is_sniping_enabled    = false;
}

/**
 * \brief Save the value of `config` to eeprom.
 *
 * Note that all values are written verbatim, including whether drag-scroll
 * and/or sniper mode are enabled.  `read_charybdis_config_from_eeprom(…)`
 * resets these 2 values to `false` since it does not make sense to persist
 * these across reboots of the board.
 */
static void write_charybdis_config_to_eeprom(charybdis_config_t* config) {
    eeconfig_update_kb(config->raw);
}

/** \brief Return the current value of the pointer's default DPI. */
static uint16_t get_pointer_default_dpi(charybdis_config_t* config) {
    return (uint16_t)config->pointer_default_dpi * CHARYBDIS_DEFAULT_DPI_CONFIG_STEP + CHARYBDIS_MINIMUM_DEFAULT_DPI;
}

/** \brief Return the current value of the pointer's sniper-mode DPI. */
static uint16_t get_pointer_sniping_dpi(charybdis_config_t* config) {
    return (uint16_t)config->pointer_sniping_dpi * CHARYBDIS_SNIPING_DPI_CONFIG_STEP + CHARYBDIS_MINIMUM_SNIPING_DPI;
}

/** \brief Set the appropriate DPI for the input config and specify the side. */
static void maybe_update_pointing_device_cpi(charybdis_config_t* config, bool is_left) {
    if (config->is_dragscroll_enabled) {
        pointing_device_set_cpi_on_side(is_left, CHARYBDIS_DRAGSCROLL_DPI);
    } else if (config->is_sniping_enabled) {
        pointing_device_set_cpi_on_side(is_left, get_pointer_sniping_dpi(config));
    } else {
        pointing_device_set_cpi_on_side(is_left, get_pointer_default_dpi(config));
    }
}

/**
 * \brief Update the pointer's default DPI to the next or previous step for a specified side.
 *
 * @param config Configuration for the trackball.
 * @param forward If `true`, increase DPI; if `false`, decrease DPI.
 * @param is_left If `true`, update the left side; if `false`, update the right side.
 */
static void step_pointer_default_dpi(charybdis_config_t* config, bool forward, bool is_left) {
    config->pointer_default_dpi += forward ? 1 : -1;

    // Pass the side information to the CPI update function.
    maybe_update_pointing_device_cpi(config, is_left);
}


/**
 * \brief Update the pointer's sniper-mode DPI to the next or previous step.
 *
 * Increases the DPI value if `forward` is `true`, decreases it otherwise.
 * The increment/decrement steps are equal to CHARYBDIS_SNIPING_DPI_CONFIG_STEP.
 */
static void step_pointer_sniping_dpi(charybdis_config_t* config, bool forward, bool is_left) {
    config->pointer_sniping_dpi += forward ? 1 : -1;

    // Pass the side information to the CPI update function.
    maybe_update_pointing_device_cpi(config, is_left);
}

uint16_t charybdis_get_pointer_default_dpi(void) {
    return get_pointer_default_dpi(&g_charybdis_config);
}

uint16_t charybdis_get_pointer_sniping_dpi(void) {
    return get_pointer_sniping_dpi(&g_charybdis_config);
}

// void charybdis_cycle_pointer_default_dpi_noeeprom(bool forward) {
//     step_pointer_default_dpi(&g_charybdis_config, forward);
// }

void charybdis_cycle_pointer_default_dpi(bool forward, bool is_left) {
    // Select the correct configuration based on 'is_left'
    charybdis_config_t* config = is_left ? &g_charybdis_config_left : &g_charybdis_config_right;

    // Pass the correct configuration and the 'is_left' flag
    step_pointer_default_dpi(config, forward, is_left);

    // Write the updated configuration to EEPROM
    write_charybdis_config_to_eeprom(config);
}



// void charybdis_cycle_pointer_sniping_dpi_noeeprom(bool forward) {
//     step_pointer_sniping_dpi(&g_charybdis_config, forward);
// }

void charybdis_cycle_pointer_sniping_dpi(bool forward, bool is_left) {
    // Select the correct configuration based on 'is_left'
    charybdis_config_t* config = is_left ? &g_charybdis_config_left : &g_charybdis_config_right;

    // Pass the correct configuration and the 'is_left' flag
    step_pointer_sniping_dpi(config, forward, is_left);

    // Write the updated configuration to EEPROM
    write_charybdis_config_to_eeprom(config);
}

bool charybdis_get_pointer_sniping_enabled(bool is_left) {
    charybdis_config_t* config = is_left ? &g_charybdis_config_left : &g_charybdis_config_right;
    return config->is_sniping_enabled;
}

void charybdis_set_pointer_sniping_enabled(bool enable, bool is_left) {
    // Select the correct configuration based on 'is_left'
    charybdis_config_t* config = is_left ? &g_charybdis_config_left : &g_charybdis_config_right;

    // Update the sniping enabled state for the specified side
    config->is_sniping_enabled = enable;

    // Apply the updated configuration
    maybe_update_pointing_device_cpi(config, is_left);
}


bool charybdis_get_pointer_dragscroll_enabled(bool is_left) {
    charybdis_config_t* config = is_left ? &g_charybdis_config_left : &g_charybdis_config_right;
    return config->is_dragscroll_enabled;
}

void charybdis_set_pointer_dragscroll_enabled(bool enable, bool is_left) {
    charybdis_config_t* config = is_left ? &g_charybdis_config_left : &g_charybdis_config_right;
    config->is_dragscroll_enabled = enable;
    maybe_update_pointing_device_cpi(config, is_left);
}

/**
 * \brief Augment the pointing device behavior.
 *
 * Implement drag-scroll.
 */
static void pointing_device_task_charybdis(report_mouse_t* mouse_report, bool is_left) {
    static int16_t scroll_buffer_x = 0;
    static int16_t scroll_buffer_y = 0;
    if (charybdis_get_pointer_dragscroll_enabled(is_left)) {
#    ifdef CHARYBDIS_DRAGSCROLL_REVERSE_X
        scroll_buffer_x -= mouse_report->x;
#    else
        scroll_buffer_x += mouse_report->x;
#    endif // CHARYBDIS_DRAGSCROLL_REVERSE_X
#    ifdef CHARYBDIS_DRAGSCROLL_REVERSE_Y
        scroll_buffer_y -= mouse_report->y;
#    else
        scroll_buffer_y += mouse_report->y;
#    endif // CHARYBDIS_DRAGSCROLL_REVERSE_Y
        mouse_report->x = 0;
        mouse_report->y = 0;
        if (abs(scroll_buffer_x) > CHARYBDIS_DRAGSCROLL_BUFFER_SIZE) {
            mouse_report->h = scroll_buffer_x > 0 ? 1 : -1;
            scroll_buffer_x = 0;
        }
        if (abs(scroll_buffer_y) > CHARYBDIS_DRAGSCROLL_BUFFER_SIZE) {
            mouse_report->v = scroll_buffer_y > 0 ? 1 : -1;
            scroll_buffer_y = 0;
        }
    }
}

report_mouse_t pointing_device_task_combined_kb(report_mouse_t left_report, report_mouse_t right_report) {
        pointing_device_task_charybdis(&left_report, true);
        pointing_device_task_charybdis(&right_report, false);
    return pointing_device_combine_reports(left_report, right_report);
}

#    if defined(POINTING_DEVICE_ENABLE) && !defined(NO_CHARYBDIS_KEYCODES)
/** \brief Whether SHIFT mod is enabled. */
static bool has_shift_mod(void) {
#        ifdef NO_ACTION_ONESHOT
    return mod_config(get_mods()) & MOD_MASK_SHIFT;
#        else
    return mod_config(get_mods() | get_oneshot_mods()) & MOD_MASK_SHIFT;
#        endif // NO_ACTION_ONESHOT
}
#    endif // POINTING_DEVICE_ENABLE && !NO_CHARYBDIS_KEYCODES

/**
 * \brief Outputs the Charybdis configuration to console.
 *
 * Prints the in-memory configuration structure to console, for debugging.
 * Includes:
 *   - raw value
 *   - drag-scroll: on/off
 *   - sniping: on/off
 *   - default DPI: internal table index/actual DPI
 *   - sniping DPI: internal table index/actual DPI
 */
static void debug_charybdis_config_to_console(charybdis_config_t* config) {
#    ifdef CONSOLE_ENABLE
    dprintf("(charybdis) process_record_kb: config = {\n"
            "\traw = 0x%X,\n"
            "\t{\n"
            "\t\tis_dragscroll_enabled=%u\n"
            "\t\tis_sniping_enabled=%u\n"
            "\t\tdefault_dpi=0x%X (%u)\n"
            "\t\tsniping_dpi=0x%X (%u)\n"
            "\t}\n"
            "}\n",
            config->raw, config->is_dragscroll_enabled, config->is_sniping_enabled, config->pointer_default_dpi, get_pointer_default_dpi(config), config->pointer_sniping_dpi, get_pointer_sniping_dpi(config));
#    endif // CONSOLE_ENABLE
}

bool process_record_kb(uint16_t keycode, keyrecord_t* record) {
    if (!process_record_user(keycode, record)) {
        debug_charybdis_config_to_console(&g_charybdis_config);
        return false;
    }
#ifdef POINTING_DEVICE_ENABLE
#ifndef NO_CHARYBDIS_KEYCODES
    switch (keycode) {
        case LEFT_POINTER_DEFAULT_DPI_FORWARD:
            if (record->event.pressed) {
                charybdis_cycle_pointer_default_dpi(!has_shift_mod(), true);
            }
            break;
        case RIGHT_POINTER_DEFAULT_DPI_FORWARD:
            if (record->event.pressed) {
                charybdis_cycle_pointer_default_dpi(!has_shift_mod(), false);
            }
            break;
        case LEFT_POINTER_DEFAULT_DPI_REVERSE:
            if (record->event.pressed) {
                charybdis_cycle_pointer_default_dpi(has_shift_mod(), true);
            }
            break;
        case RIGHT_POINTER_DEFAULT_DPI_REVERSE:
            if (record->event.pressed) {
                charybdis_cycle_pointer_default_dpi(has_shift_mod(), false);
            }
            break;
        case LEFT_POINTER_SNIPING_DPI_FORWARD:
            if (record->event.pressed) {
                // Step backward if shifted, forward otherwise.
                charybdis_cycle_pointer_sniping_dpi(/* forward= */ !has_shift_mod(), true);
            }
            break;
        case RIGHT_POINTER_SNIPING_DPI_FORWARD:
            if (record->event.pressed) {
                // Step backward if shifted, forward otherwise.
                charybdis_cycle_pointer_sniping_dpi(/* forward= */ !has_shift_mod(), false);
            }
            break;
        case LEFT_POINTER_SNIPING_DPI_REVERSE:
            if (record->event.pressed) {
                // Step forward if shifted, backward otherwise.
                charybdis_cycle_pointer_sniping_dpi(/* forward= */ has_shift_mod(), true);
            }
            break;
        case RIGHT_POINTER_SNIPING_DPI_REVERSE:
            if (record->event.pressed) {
                // Step forward if shifted, backward otherwise.
                charybdis_cycle_pointer_sniping_dpi(/* forward= */ has_shift_mod(), false);
            }
            break;
        case LEFT_SNIPING_MODE:
            charybdis_set_pointer_sniping_enabled(record->event.pressed, true);
            break;
        case RIGHT_SNIPING_MODE:
            charybdis_set_pointer_sniping_enabled(record->event.pressed, false);
            break;
        case LEFT_SNIPING_MODE_TOGGLE:
            if (record->event.pressed) {
                charybdis_set_pointer_sniping_enabled(!charybdis_get_pointer_sniping_enabled(true), true);
            }
            break;
        case RIGHT_SNIPING_MODE_TOGGLE:
            if (record->event.pressed) {
                charybdis_set_pointer_sniping_enabled(!charybdis_get_pointer_sniping_enabled(false), false);
            }
            break;
        case LEFT_DRAGSCROLL_MODE:
            charybdis_set_pointer_dragscroll_enabled(record->event.pressed, true);
            break;
        case RIGHT_DRAGSCROLL_MODE:
            charybdis_set_pointer_dragscroll_enabled(record->event.pressed, false);
            break;
        case LEFT_DRAGSCROLL_MODE_TOGGLE:
            if (record->event.pressed) {
                charybdis_set_pointer_dragscroll_enabled(!charybdis_get_pointer_dragscroll_enabled(true), true);
            }
            break;
        case RIGHT_DRAGSCROLL_MODE_TOGGLE:
            if (record->event.pressed) {
                charybdis_set_pointer_dragscroll_enabled(!charybdis_get_pointer_dragscroll_enabled(false), false);
            }
            break;
    }
#        endif // !NO_CHARYBDIS_KEYCODES
#    endif     // POINTING_DEVICE_ENABLE
    if (IS_QK_KB(keycode) || IS_MOUSEKEY(keycode)) {
        debug_charybdis_config_to_console(&g_charybdis_config);
    }
    return true;
}


void eeconfig_init_kb(void) {
    g_charybdis_config.raw = 0;
    write_charybdis_config_to_eeprom(&g_charybdis_config);
    maybe_update_pointing_device_cpi(&g_charybdis_config_left, true);
    maybe_update_pointing_device_cpi(&g_charybdis_config_right, false);
    eeconfig_init_user();
}

void matrix_init_kb(void) {
    read_charybdis_config_from_eeprom(&g_charybdis_config);
    matrix_init_user();
}

#    ifdef CHARYBDIS_CONFIG_SYNC
void charybdis_config_sync_handler(uint8_t initiator2target_buffer_size, const void* initiator2target_buffer, uint8_t target2initiator_buffer_size, void* target2initiator_buffer) {
    if (initiator2target_buffer_size == sizeof(g_charybdis_config)) {
        memcpy(&g_charybdis_config, initiator2target_buffer, sizeof(g_charybdis_config));
    }
}
#    endif

#ifdef CHARYBDIS_CONFIG_DUAL_SYNC
void charybdis_config_dual_sync_handler(uint8_t initiator2target_buffer_size, const void* initiator2target_buffer, uint8_t target2initiator_buffer_size, void* target2initiator_buffer) {
    if (initiator2target_buffer_size == sizeof(g_charybdis_config_left.raw) + sizeof(g_charybdis_config_right.raw)) {
        // Copy left configuration
        memcpy(&g_charybdis_config_left, initiator2target_buffer, sizeof(g_charybdis_config_left));
        // Copy right configuration
        memcpy(&g_charybdis_config_right, (const uint8_t*)initiator2target_buffer + sizeof(g_charybdis_config_left), sizeof(g_charybdis_config_right));
    }
}
#endif


void keyboard_post_init_kb(void) {
    maybe_update_pointing_device_cpi(&g_charybdis_config_left, true);
    maybe_update_pointing_device_cpi(&g_charybdis_config_right, false);
#    ifdef CHARYBDIS_CONFIG_DUAL_SYNC
    transaction_register_rpc(RPC_ID_KB_CONFIG_DUAL_SYNC, charybdis_config_dual_sync_handler);
#    endif
    keyboard_post_init_user();
}

#    ifdef CHARYBDIS_CONFIG_SYNC
void housekeeping_task_kb(void) {
    if (is_keyboard_master()) {
        // Keep track of the last state, so that we can tell if we need to propagate to slave.
        static charybdis_config_t last_charybdis_config = {0};
        static uint32_t           last_sync             = 0;
        bool                      needs_sync            = false;

        // Check if the state values are different.
        if (memcmp(&g_charybdis_config, &last_charybdis_config, sizeof(g_charybdis_config))) {
            needs_sync = true;
            memcpy(&last_charybdis_config, &g_charybdis_config, sizeof(g_charybdis_config));
        }
        // Send to slave every 500ms regardless of state change.
        if (timer_elapsed32(last_sync) > 500) {
            needs_sync = true;
        }

        // Perform the sync if requested.
        if (needs_sync) {
            if (transaction_rpc_send(RPC_ID_KB_CONFIG_SYNC, sizeof(g_charybdis_config), &g_charybdis_config)) {
                last_sync = timer_read32();
            }
        }
    }
    // No need to invoke the user-specific callback, as it's been called
    // already.
}
#    endif // CHARYBDIS_CONFIG_SYNC

#ifdef CHARYBDIS_CONFIG_DUAL_SYNC
void housekeeping_task_kb(void) {
    if (is_keyboard_master()) {
        // Keep track of the last state, so that we can tell if we need to propagate to slave.
        static charybdis_config_t last_charybdis_config_left = {0};
        static charybdis_config_t last_charybdis_config_right = {0};
        static uint32_t last_sync = 0;
        bool needs_sync = false;

        // Check if the left state values are different.
        if (memcmp(&g_charybdis_config_left, &last_charybdis_config_left, sizeof(g_charybdis_config_left))) {
            needs_sync = true;
            memcpy(&last_charybdis_config_left, &g_charybdis_config_left, sizeof(g_charybdis_config_left));
        }

        // Check if the right state values are different.
        if (memcmp(&g_charybdis_config_right, &last_charybdis_config_right, sizeof(g_charybdis_config_right))) {
            needs_sync = true;
            memcpy(&last_charybdis_config_right, &g_charybdis_config_right, sizeof(g_charybdis_config_right));
        }

        // Send to slave every 500ms regardless of state change.
        if (timer_elapsed32(last_sync) > 500) {
            needs_sync = true;
        }

        // Perform the sync if requested.
        if (needs_sync) {
            uint8_t sync_buffer[sizeof(g_charybdis_config_left) + sizeof(g_charybdis_config_right)];
            memcpy(sync_buffer, &g_charybdis_config_left, sizeof(g_charybdis_config_left));
            memcpy(sync_buffer + sizeof(g_charybdis_config_left), &g_charybdis_config_right, sizeof(g_charybdis_config_right));
            if (transaction_rpc_send(RPC_ID_KB_CONFIG_DUAL_SYNC, sizeof(sync_buffer), sync_buffer)) {
                last_sync = timer_read32();
            }
        }
    }
    // No need to invoke the user-specific callback, as it's been called already.
}
#endif // CHARYBDIS_CONFIG_DUAL_SYNC


#endif     // POINTING_DEVICE_ENABLE
