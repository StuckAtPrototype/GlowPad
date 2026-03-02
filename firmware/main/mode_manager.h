/**
 * @file mode_manager.h
 * @brief Application mode manager
 *
 * Manages switching between Pomodoro, Piano, and Memory Game modes.
 * Long-pressing keys 0-2 switches modes; all other button events are
 * forwarded to the currently active mode handler.
 */

#ifndef MODE_MANAGER_H
#define MODE_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MODE_POMODORO = 0,
    MODE_PIANO,
    MODE_MEMORY,
    MODE_COUNT
} app_mode_t;

/**
 * @brief Initialise the mode manager (starts in Pomodoro mode)
 */
void mode_manager_init(void);

/**
 * @brief Process a button event
 *
 * Long press on keys 0-2 triggers a mode switch.
 * Everything else is forwarded to the active mode handler.
 */
void mode_manager_handle_button(uint8_t button_id, bool is_long_press);

/**
 * @brief Periodic update – call from the main loop (~100 ms)
 *
 * Delegates to the active mode's update function (timer tick,
 * LED animation, etc.).
 */
void mode_manager_update(void);

/**
 * @brief Get the currently active mode
 */
app_mode_t mode_manager_get_mode(void);

#ifdef __cplusplus
}
#endif

#endif // MODE_MANAGER_H
