/**
 * @file reaction_game.h
 * @brief Reaction speed game mode
 *
 * A random LED lights up after a random delay; the player must press the
 * matching key as fast as possible.  Runs 5 rounds, then shows a final score.
 */

#ifndef REACTION_GAME_H
#define REACTION_GAME_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void reaction_game_init(void);
void reaction_game_enter(void);
void reaction_game_handle_button(uint8_t button_id, bool is_long_press);
void reaction_game_update(void);
void reaction_game_reset(void);

#ifdef __cplusplus
}
#endif

#endif // REACTION_GAME_H
