/**
 * @file media_control.h
 * @brief Media control mode – USB HID consumer (keyboard/media keys)
 *
 * Maps the five pads to media keys: Previous, Play/Pause, Next,
 * Volume Down, Volume Up. Requires USB connection (native USB on ESP32-S3).
 */

#ifndef MEDIA_CONTROL_H
#define MEDIA_CONTROL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One-time initialisation (call once at boot). Initialises USB HID.
 */
void media_control_init(void);

/**
 * @brief Enter media control mode – set idle LED pattern
 */
void media_control_enter(void);

/**
 * @brief Handle a button event: send corresponding HID consumer key
 */
void media_control_handle_button(uint8_t button_id, bool is_long_press);

/**
 * @brief Periodic update (~100 ms) – LED feedback
 */
void media_control_update(void);

/**
 * @brief Reset state (called when leaving media mode)
 */
void media_control_reset(void);

#ifdef __cplusplus
}
#endif

#endif // MEDIA_CONTROL_H
