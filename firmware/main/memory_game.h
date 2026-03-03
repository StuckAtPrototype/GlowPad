/**
 * @file memory_game.h
 * @brief Simon-style memory game mode
 *
 * The system plays an ever-growing random sequence of lights and tones.
 * The player must repeat the sequence.  Score is shown on the LEDs
 * between rounds.
 */

#ifndef MEMORY_GAME_H
#define MEMORY_GAME_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One-time initialisation (call once at boot)
 */
void memory_game_init(void);

/**
 * @brief Enter memory-game mode -- show idle pattern, reset state
 */
void memory_game_enter(void);

/**
 * @brief Handle a button event while in memory-game mode
 */
void memory_game_handle_button(uint8_t button_id, bool is_long_press);

/**
 * @brief Periodic update (~100 ms) -- drives the game state machine
 */
void memory_game_update(void);

/**
 * @brief Reset game state (called when leaving this mode)
 */
void memory_game_reset(void);

#ifdef __cplusplus
}
#endif

#endif // MEMORY_GAME_H
