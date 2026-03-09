/*
 * SPDX-FileCopyrightText: 2026 StuckAtPrototype
 * SPDX-License-Identifier: CC0-1.0
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "led_driver.h"
#include "led_strip_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "esp_log.h"

#define LED_GPIO                21
#define RMT_RESOLUTION_HZ       10000000

static const char *TAG = "led";

static rmt_channel_handle_t s_led_chan;
static rmt_encoder_handle_t s_led_encoder;
static rmt_transmit_config_t s_tx_config;
static uint8_t s_pixel_buf[LED_COUNT * 3];

esp_err_t led_driver_init(void)
{
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = LED_GPIO,
        .mem_block_symbols = 64,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .trans_queue_depth = 4,
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_chan_config, &s_led_chan), TAG, "create RMT TX channel failed");

    led_strip_encoder_config_t encoder_config = {
        .resolution = RMT_RESOLUTION_HZ,
    };
    ESP_RETURN_ON_ERROR(rmt_new_led_strip_encoder(&encoder_config, &s_led_encoder), TAG, "create LED encoder failed");

    ESP_RETURN_ON_ERROR(rmt_enable(s_led_chan), TAG, "enable RMT channel failed");

    s_tx_config = (rmt_transmit_config_t){ .loop_count = 0 };

    memset(s_pixel_buf, 0, sizeof(s_pixel_buf));
    ESP_RETURN_ON_ERROR(rmt_transmit(s_led_chan, s_led_encoder, s_pixel_buf, sizeof(s_pixel_buf), &s_tx_config), TAG, "initial clear failed");
    ESP_RETURN_ON_ERROR(rmt_tx_wait_all_done(s_led_chan, portMAX_DELAY), TAG, "wait for clear failed");

    ESP_LOGI(TAG, "LED driver ready: %d LEDs on IO%d", LED_COUNT, LED_GPIO);
    return ESP_OK;
}

esp_err_t led_driver_set(const rgb_t colors[LED_COUNT])
{
    for (int i = 0; i < LED_COUNT; i++) {
        s_pixel_buf[i * 3 + 0] = colors[i].g;
        s_pixel_buf[i * 3 + 1] = colors[i].r;
        s_pixel_buf[i * 3 + 2] = colors[i].b;
    }
    ESP_RETURN_ON_ERROR(rmt_transmit(s_led_chan, s_led_encoder, s_pixel_buf, sizeof(s_pixel_buf), &s_tx_config), TAG, "transmit failed");
    ESP_RETURN_ON_ERROR(rmt_tx_wait_all_done(s_led_chan, portMAX_DELAY), TAG, "wait failed");
    return ESP_OK;
}
