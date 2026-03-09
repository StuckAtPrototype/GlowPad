/*
 * SPDX-FileCopyrightText: 2026 StuckAtPrototype
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

#define LED_COUNT 5

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb_t;

esp_err_t led_driver_init(void);
esp_err_t led_driver_set(const rgb_t colors[LED_COUNT]);
