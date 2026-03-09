/*
 * SPDX-FileCopyrightText: 2026 StuckAtPrototype
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/touch_pad.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "led_driver.h"
#include "piezo_driver.h"

#define TOUCH_POLL_MS                20
#define TOUCH_CALIBRATION_SAMPLES    64
#define TOUCH_LONG_PRESS_MS          700
#define TOUCH_PRESS_PERCENT          2
#define TOUCH_RELEASE_PERCENT        1
#define TOUCH_DEBOUNCE_PRESS         3
#define TOUCH_DEBOUNCE_RELEASE       3

typedef struct {
    const char *name;
    touch_pad_t channel;
    uint32_t baseline;
    uint32_t last_raw;
    uint32_t press_delta;
    uint32_t release_delta;
    bool pressed;
    bool long_reported;
    TickType_t press_start_ticks;
    uint8_t touch_streak;
    uint8_t release_streak;
} cap_input_t;

static const char *TAG = "touch";

static cap_input_t s_caps[] = {
    { .name = "CAP_1", .channel = TOUCH_PAD_NUM4 },
    { .name = "CAP_2", .channel = TOUCH_PAD_NUM5 },
    { .name = "CAP_3", .channel = TOUCH_PAD_NUM3 },
    { .name = "CAP_4", .channel = TOUCH_PAD_NUM2 },
    { .name = "CAP_5", .channel = TOUCH_PAD_NUM1 },
};

static uint32_t abs_diff_u32(uint32_t a, uint32_t b)
{
    return (a >= b) ? (a - b) : (b - a);
}

static esp_err_t configure_touch(void)
{
    ESP_RETURN_ON_ERROR(touch_pad_init(), TAG, "touch_pad_init failed");
    ESP_RETURN_ON_ERROR(
        touch_pad_set_voltage(TOUCH_HVOLT_2V7, TOUCH_LVOLT_0V5, TOUCH_HVOLT_ATTEN_1V),
        TAG,
        "touch_pad_set_voltage failed"
    );

    for (size_t i = 0; i < sizeof(s_caps) / sizeof(s_caps[0]); ++i) {
        ESP_RETURN_ON_ERROR(touch_pad_config(s_caps[i].channel), TAG, "touch_pad_config failed");
    }

    ESP_RETURN_ON_ERROR(touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER), TAG, "set_fsm_mode failed");
    ESP_RETURN_ON_ERROR(touch_pad_fsm_start(), TAG, "touch_pad_fsm_start failed");

    return ESP_OK;
}

static esp_err_t calibrate_touch(void)
{
    for (size_t i = 0; i < sizeof(s_caps) / sizeof(s_caps[0]); ++i) {
        uint64_t sum = 0;
        uint32_t min_raw = UINT32_MAX;
        uint32_t max_raw = 0;
        for (int sample = 0; sample < TOUCH_CALIBRATION_SAMPLES; ++sample) {
            uint32_t raw = 0;
            ESP_RETURN_ON_ERROR(
                touch_pad_read_raw_data(s_caps[i].channel, &raw),
                TAG,
                "touch_pad_read_raw_data failed"
            );
            sum += raw;
            if (raw < min_raw) {
                min_raw = raw;
            }
            if (raw > max_raw) {
                max_raw = raw;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        s_caps[i].baseline = (uint32_t)(sum / TOUCH_CALIBRATION_SAMPLES);
        s_caps[i].last_raw = s_caps[i].baseline;
        s_caps[i].press_delta = (s_caps[i].baseline * TOUCH_PRESS_PERCENT) / 100U;
        s_caps[i].release_delta = (s_caps[i].baseline * TOUCH_RELEASE_PERCENT) / 100U;
        ESP_LOGI(
            TAG,
            "%s baseline=%" PRIu32 " noise=%" PRIu32 " press_delta=%" PRIu32 " release_delta=%" PRIu32,
            s_caps[i].name,
            s_caps[i].baseline,
            max_raw - min_raw,
            s_caps[i].press_delta,
            s_caps[i].release_delta
        );
    }
    return ESP_OK;
}

static void process_touch_inputs(void)
{
    const TickType_t now_ticks = xTaskGetTickCount();

    for (size_t i = 0; i < sizeof(s_caps) / sizeof(s_caps[0]); ++i) {
        uint32_t raw = 0;
        if (touch_pad_read_raw_data(s_caps[i].channel, &raw) != ESP_OK) {
            continue;
        }
        s_caps[i].last_raw = raw;

        const uint32_t delta = abs_diff_u32(raw, s_caps[i].baseline);
        const bool touched_sample = s_caps[i].pressed ? (delta >= s_caps[i].release_delta) : (delta >= s_caps[i].press_delta);
        if (touched_sample) {
            if (s_caps[i].touch_streak < TOUCH_DEBOUNCE_PRESS + 1) {
                s_caps[i].touch_streak++;
            }
            s_caps[i].release_streak = 0;
        } else {
            if (s_caps[i].release_streak < TOUCH_DEBOUNCE_RELEASE + 1) {
                s_caps[i].release_streak++;
            }
            s_caps[i].touch_streak = 0;
        }

        if (!s_caps[i].pressed && s_caps[i].touch_streak >= TOUCH_DEBOUNCE_PRESS) {
            s_caps[i].pressed = true;
            s_caps[i].long_reported = false;
            s_caps[i].press_start_ticks = now_ticks;
        }

        if (s_caps[i].pressed && !s_caps[i].long_reported) {
            const TickType_t held_ticks = now_ticks - s_caps[i].press_start_ticks;
            const int64_t held_ms = (int64_t)(held_ticks * portTICK_PERIOD_MS);
            if (held_ms >= (int64_t)TOUCH_LONG_PRESS_MS) {
                ESP_LOGI(TAG, "%s LONG", s_caps[i].name);
                s_caps[i].long_reported = true;
            }
        }

        if (s_caps[i].pressed && s_caps[i].release_streak >= TOUCH_DEBOUNCE_RELEASE) {
            const TickType_t held_ticks = now_ticks - s_caps[i].press_start_ticks;
            const int64_t held_ms = (int64_t)(held_ticks * portTICK_PERIOD_MS);
            if (!s_caps[i].long_reported && held_ms < (int64_t)TOUCH_LONG_PRESS_MS) {
                ESP_LOGI(TAG, "%s SHORT", s_caps[i].name);
            }
            s_caps[i].pressed = false;
            s_caps[i].long_reported = false;
            s_caps[i].touch_streak = 0;
            s_caps[i].release_streak = 0;
        }
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(configure_touch());
    ESP_LOGI(TAG, "Touch initialized for CAP_1..CAP_5 (IO4,IO5,IO3,IO2,IO1)");
    ESP_LOGI(TAG, "Do not touch pads during startup calibration...");
    ESP_ERROR_CHECK(calibrate_touch());
    ESP_LOGI(TAG, "Ready: SHORT < %dms, LONG >= %dms", TOUCH_LONG_PRESS_MS, TOUCH_LONG_PRESS_MS);

    ESP_ERROR_CHECK(led_driver_init());
    const rgb_t startup_colors[LED_COUNT] = {
        {10, 10, 10}, {10, 10, 10}, {10, 10, 10}, {10, 10, 10}, {10, 10, 10},
    };
    ESP_ERROR_CHECK(led_driver_set(startup_colors));

    ESP_ERROR_CHECK(piezo_init());
    const piezo_note_t startup_jingle[] = {
        { NOTE_C5, 100 },
        { NOTE_E5, 100 },
        { NOTE_G5, 100 },
        { NOTE_C6, 200 },
    };
    ESP_ERROR_CHECK(piezo_play_jingle(startup_jingle, sizeof(startup_jingle) / sizeof(startup_jingle[0])));

    while (true) {
        process_touch_inputs();
        vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_MS));
    }
}
