/*
 * SPDX-FileCopyrightText: 2026 StuckAtPrototype
 * SPDX-License-Identifier: CC0-1.0
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "piezo_driver.h"

#define PIEZO_GPIO          48
#define PIEZO_LEDC_TIMER    LEDC_TIMER_0
#define PIEZO_LEDC_CHANNEL  LEDC_CHANNEL_0
#define PIEZO_LEDC_MODE     LEDC_LOW_SPEED_MODE
#define PIEZO_DUTY_RES      LEDC_TIMER_10_BIT
#define PIEZO_DUTY_50PCT    ((1 << 10) / 2)
#define PIEZO_NOTE_GAP_MS   30

static const char *TAG = "piezo";

esp_err_t piezo_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode = PIEZO_LEDC_MODE,
        .duty_resolution = PIEZO_DUTY_RES,
        .timer_num = PIEZO_LEDC_TIMER,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_cfg), TAG, "timer config failed");

    ledc_channel_config_t chan_cfg = {
        .speed_mode = PIEZO_LEDC_MODE,
        .channel = PIEZO_LEDC_CHANNEL,
        .timer_sel = PIEZO_LEDC_TIMER,
        .gpio_num = PIEZO_GPIO,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&chan_cfg), TAG, "channel config failed");

    ESP_LOGI(TAG, "Piezo ready on IO%d", PIEZO_GPIO);
    return ESP_OK;
}

esp_err_t piezo_tone_on(uint32_t freq_hz)
{
    if (freq_hz == 0) {
        return piezo_tone_off();
    }
    ESP_RETURN_ON_ERROR(ledc_set_freq(PIEZO_LEDC_MODE, PIEZO_LEDC_TIMER, freq_hz), TAG, "set freq failed");
    ESP_RETURN_ON_ERROR(ledc_set_duty(PIEZO_LEDC_MODE, PIEZO_LEDC_CHANNEL, PIEZO_DUTY_50PCT), TAG, "set duty failed");
    ESP_RETURN_ON_ERROR(ledc_update_duty(PIEZO_LEDC_MODE, PIEZO_LEDC_CHANNEL), TAG, "update duty failed");
    return ESP_OK;
}

esp_err_t piezo_tone_off(void)
{
    ESP_RETURN_ON_ERROR(ledc_set_duty(PIEZO_LEDC_MODE, PIEZO_LEDC_CHANNEL, 0), TAG, "set duty 0 failed");
    ESP_RETURN_ON_ERROR(ledc_update_duty(PIEZO_LEDC_MODE, PIEZO_LEDC_CHANNEL), TAG, "update duty failed");
    return ESP_OK;
}

esp_err_t piezo_play(uint32_t freq_hz, uint32_t duration_ms)
{
    ESP_RETURN_ON_ERROR(piezo_tone_on(freq_hz), TAG, "tone_on failed");
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    ESP_RETURN_ON_ERROR(piezo_tone_off(), TAG, "tone_off failed");
    return ESP_OK;
}

esp_err_t piezo_play_jingle(const piezo_note_t notes[], size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (notes[i].freq_hz == NOTE_REST) {
            vTaskDelay(pdMS_TO_TICKS(notes[i].duration_ms));
        } else {
            ESP_RETURN_ON_ERROR(piezo_play(notes[i].freq_hz, notes[i].duration_ms), TAG, "play note failed");
        }
        if (i + 1 < count) {
            vTaskDelay(pdMS_TO_TICKS(PIEZO_NOTE_GAP_MS));
        }
    }
    return ESP_OK;
}
