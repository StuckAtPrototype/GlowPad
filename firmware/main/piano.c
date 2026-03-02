/**
 * @file piano.c
 * @brief Piano mode – 5-key pentatonic keyboard
 *
 * Maps the five capacitive touch pads to a C-major pentatonic scale
 * (C5-D5-E5-G5-A5) and provides per-key LED feedback with a rainbow
 * colour scheme.
 */

#include "piano.h"
#include "led.h"
#include "piezo.h"
#include "button.h"
#include "ws2812_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "piano";

// C-major pentatonic: C5, D5, E5, G5, A5
static const uint32_t note_freqs[NUM_BUTTONS] = {
    523, 587, 659, 784, 880
};

#define NOTE_DURATION_MS 200

// Rainbow colours per key (GRB format, full brightness)
static const uint32_t key_colors_bright[NUM_BUTTONS] = {
    0x00FF00,   // key 0 – Red
    0xFFFF00,   // key 1 – Yellow
    0xFF0000,   // key 2 – Green
    0xFF00FF,   // key 3 – Cyan
    0x0000FF,   // key 4 – Blue
};

// Same colours at ~20 % brightness (divide each channel by 5)
static const uint32_t key_colors_dim[NUM_BUTTONS] = {
    0x003300,   // dim Red
    0x333300,   // dim Yellow
    0x330000,   // dim Green
    0x330033,   // dim Cyan
    0x000033,   // dim Blue
};

// Timestamp of the last press per key (0 = idle)
static TickType_t key_press_tick[NUM_BUTTONS] = {0};

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static void set_idle_leds(void)
{
    led_set_intensity(1.0f);
    for (int i = 0; i < NUM_BUTTONS; i++) {
        led_set_led_color((uint8_t)i, key_colors_dim[i]);
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void piano_init(void)
{
    for (int i = 0; i < NUM_BUTTONS; i++) {
        key_press_tick[i] = 0;
    }
}

void piano_enter(void)
{
    for (int i = 0; i < NUM_BUTTONS; i++) {
        key_press_tick[i] = 0;
    }
    set_idle_leds();
    ESP_LOGI(TAG, "Piano mode active");
}

void piano_handle_button(uint8_t button_id, bool is_long_press)
{
    if (is_long_press || button_id >= NUM_BUTTONS) {
        return;
    }

    ESP_LOGI(TAG, "Key %d -> %lu Hz", button_id, note_freqs[button_id]);

    piezo_play_tone(note_freqs[button_id], NOTE_DURATION_MS);

    led_set_led_color(button_id, key_colors_bright[button_id]);
    key_press_tick[button_id] = xTaskGetTickCount();
}

void piano_update(void)
{
    TickType_t now = xTaskGetTickCount();

    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (key_press_tick[i] != 0) {
            uint32_t elapsed_ms = ((now - key_press_tick[i]) * 1000) / configTICK_RATE_HZ;
            if (elapsed_ms >= NOTE_DURATION_MS) {
                led_set_led_color((uint8_t)i, key_colors_dim[i]);
                key_press_tick[i] = 0;
            }
        }
    }
}

void piano_reset(void)
{
    piezo_stop();
    for (int i = 0; i < NUM_BUTTONS; i++) {
        key_press_tick[i] = 0;
    }
}
