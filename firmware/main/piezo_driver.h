/*
 * SPDX-FileCopyrightText: 2026 StuckAtPrototype
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define NOTE_C4  262
#define NOTE_D4  294
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_G4  392
#define NOTE_A4  440
#define NOTE_B4  494
#define NOTE_C5  523
#define NOTE_D5  587
#define NOTE_E5  659
#define NOTE_F5  698
#define NOTE_G5  784
#define NOTE_A5  880
#define NOTE_B5  988
#define NOTE_C6  1047
#define NOTE_REST 0

typedef struct {
    uint32_t freq_hz;
    uint32_t duration_ms;
} piezo_note_t;

esp_err_t piezo_init(void);
esp_err_t piezo_tone_on(uint32_t freq_hz);
esp_err_t piezo_tone_off(void);
esp_err_t piezo_play(uint32_t freq_hz, uint32_t duration_ms);
esp_err_t piezo_play_jingle(const piezo_note_t notes[], size_t count);
