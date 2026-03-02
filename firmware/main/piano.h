/**
 * @file piano.h
 * @brief Piano mode – 5-key pentatonic keyboard
 *
 * Each of the 5 capacitive touch pads plays a note from the C-major
 * pentatonic scale through the piezo buzzer, with per-key LED feedback.
 */

#ifndef PIANO_H
#define PIANO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One-time initialisation (call once at boot)
 */
void piano_init(void);

/**
 * @brief Enter piano mode – set the idle rainbow LED pattern
 */
void piano_enter(void);

/**
 * @brief Handle a button event while in piano mode
 *
 * Short press plays the corresponding note; long presses are ignored
 * (mode-switch long presses are intercepted by mode_manager before
 * reaching here).
 */
void piano_handle_button(uint8_t button_id, bool is_long_press);

/**
 * @brief Periodic update (~100 ms) – fades active-key LEDs back to idle
 */
void piano_update(void);

/**
 * @brief Reset piano state (called when leaving piano mode)
 */
void piano_reset(void);

#ifdef __cplusplus
}
#endif

#endif // PIANO_H
