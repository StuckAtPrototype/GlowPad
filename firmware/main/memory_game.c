/**
 * @file memory_game.c
 * @brief Simon-style memory game implementation
 *
 * State machine: IDLE -> SHOWING -> INPUT -> SUCCESS -> SHOWING ...
 *                                        \-> GAME_OVER -> IDLE
 *
 * Each key has a colour and tone (same mapping as piano mode):
 *   Key 0: Red   + C5 (523 Hz)
 *   Key 1: Yellow + D5 (587 Hz)
 *   Key 2: Green  + E5 (659 Hz)
 *   Key 3: Cyan   + G5 (784 Hz)
 *   Key 4: Blue   + A5 (880 Hz)
 */

#include "memory_game.h"
#include "led.h"
#include "piezo.h"
#include "button.h"
#include "ws2812_control.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "memory";

/* ------------------------------------------------------------------ */
/*  Constants                                                          */
/* ------------------------------------------------------------------ */

#define MAX_SEQUENCE        50
#define SHOW_TONE_MS        400
#define SHOW_GAP_MS         200
#define INPUT_TIMEOUT_MS    3000
#define INPUT_FLASH_MS      350
#define SUCCESS_DISPLAY_MS  500
#define GAMEOVER_FLASH_MS   250
#define GAMEOVER_FLASHES    3

// Tones per key (C-major pentatonic)
static const uint32_t key_freqs[NUM_BUTTONS] = {
    523, 587, 659, 784, 880
};

// Full-brightness colours per key (GRB format)
static const uint32_t key_colors[NUM_BUTTONS] = {
    0x00FF00,   // key 0 -- Red
    0xFFFF00,   // key 1 -- Yellow
    0xFF0000,   // key 2 -- Green
    0xFF00FF,   // key 3 -- Cyan
    0x0000FF,   // key 4 -- Blue
};

// Fail tones (descending)
#define FAIL_TONE_HI   600
#define FAIL_TONE_LO   400
#define FAIL_TONE_MS   120

// Score-tier colours (GRB)
#define TIER_COLOR_1   LED_COLOR_GREEN
#define TIER_COLOR_2   LED_COLOR_YELLOW
#define TIER_COLOR_3   LED_COLOR_CYAN
#define TIER_COLOR_4   LED_COLOR_PURPLE
#define TIER_COLOR_5   LED_COLOR_WHITE

/* ------------------------------------------------------------------ */
/*  Internal state                                                     */
/* ------------------------------------------------------------------ */

typedef enum {
    MEM_IDLE,
    MEM_SHOWING,
    MEM_INPUT,
    MEM_SUCCESS,
    MEM_GAME_OVER,
} mem_state_t;

static mem_state_t state = MEM_IDLE;
static uint8_t  sequence[MAX_SEQUENCE];
static uint8_t  seq_len    = 0;        // current sequence length
static uint8_t  show_idx   = 0;        // index during SHOWING phase
static uint8_t  input_idx  = 0;        // index during INPUT phase
static uint8_t  score      = 0;        // rounds completed
static TickType_t phase_tick = 0;      // timestamp for time-based transitions
static bool     show_tone_on = false;  // true = tone+LED on, false = in gap

// GAME_OVER animation sub-state
static uint8_t  go_flash_count = 0;
static bool     go_flash_on    = false;
static bool     go_score_shown = false;

// INPUT flash tracking
static bool     input_flash_active = false;
static TickType_t input_flash_tick = 0;
static uint8_t  input_flash_key   = 0;
static bool     round_complete     = false;

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static void show_idle_leds(void)
{
    led_set_intensity(0.3f);
    led_set_color(LED_COLOR_YELLOW);
}

static void all_leds_off(void)
{
    led_set_intensity(1.0f);
    for (int i = 0; i < NUM_LEDS; i++) {
        led_set_led_color((uint8_t)i, LED_COLOR_OFF);
    }
}

static uint32_t tier_color_for_score(uint8_t s)
{
    if (s <= 5)  return TIER_COLOR_1;
    if (s <= 10) return TIER_COLOR_2;
    if (s <= 15) return TIER_COLOR_3;
    if (s <= 20) return TIER_COLOR_4;
    return TIER_COLOR_5;
}

static void show_score_leds(uint8_t s)
{
    if (s == 0) {
        all_leds_off();
        return;
    }

    uint32_t color = tier_color_for_score(s);
    uint8_t tier_base = ((s - 1) / 5) * 5;
    uint8_t filled = s - tier_base;       // 1-5

    // Dim colour (~20 %)
    uint32_t dim = ((color >> 16) & 0xFF) / 5;
    uint32_t dim_color = (dim << 16)
                       | ((((color >> 8) & 0xFF) / 5) << 8)
                       | (((color) & 0xFF) / 5);

    led_set_intensity(1.0f);
    for (int i = 0; i < NUM_LEDS; i++) {
        if (i < filled) {
            led_set_led_color((uint8_t)i, color);
        } else if (tier_base > 0) {
            led_set_led_color((uint8_t)i, dim_color);
        } else {
            led_set_led_color((uint8_t)i, LED_COLOR_OFF);
        }
    }
}

static void append_random_element(void)
{
    if (seq_len < MAX_SEQUENCE) {
        sequence[seq_len] = (uint8_t)(esp_random() % NUM_BUTTONS);
        seq_len++;
    }
}

static void start_new_game(void)
{
    seq_len   = 0;
    score     = 0;
    append_random_element();
    show_idx  = 0;
    show_tone_on = false;
    phase_tick = xTaskGetTickCount();
    state = MEM_SHOWING;
    all_leds_off();
    ESP_LOGI(TAG, "New game started");
}

static void enter_showing(void)
{
    show_idx = 0;
    show_tone_on = false;
    phase_tick = xTaskGetTickCount();
    state = MEM_SHOWING;
    all_leds_off();
}

static void enter_input(void)
{
    input_idx = 0;
    input_flash_active = false;
    round_complete = false;
    phase_tick = xTaskGetTickCount();
    state = MEM_INPUT;
    all_leds_off();
}

static void enter_success(void)
{
    score++;
    ESP_LOGI(TAG, "Round complete! Score: %d", score);
    piezo_play_tone(1200, 60);
    vTaskDelay(pdMS_TO_TICKS(80));
    piezo_play_tone(1600, 60);
    vTaskDelay(pdMS_TO_TICKS(80));
    all_leds_off();
    phase_tick = xTaskGetTickCount();
    state = MEM_SUCCESS;
}

static void enter_game_over(void)
{
    ESP_LOGI(TAG, "Game over! Final score: %d", score);
    piezo_stop();
    piezo_play_tone(FAIL_TONE_HI, FAIL_TONE_MS);
    vTaskDelay(pdMS_TO_TICKS(FAIL_TONE_MS + 40));
    piezo_play_tone(FAIL_TONE_LO, FAIL_TONE_MS);
    vTaskDelay(pdMS_TO_TICKS(FAIL_TONE_MS + 40));

    go_flash_count = 0;
    go_flash_on = true;
    go_score_shown = false;
    phase_tick = xTaskGetTickCount();
    state = MEM_GAME_OVER;
}

/* ------------------------------------------------------------------ */
/*  State-machine update (called every ~100 ms)                        */
/* ------------------------------------------------------------------ */

static void update_showing(void)
{
    TickType_t now = xTaskGetTickCount();
    uint32_t elapsed = ((now - phase_tick) * 1000) / configTICK_RATE_HZ;

    if (!show_tone_on) {
        // We are in the gap (or at the very start).  Time to show next element?
        if (elapsed >= SHOW_GAP_MS) {
            if (show_idx >= seq_len) {
                // Done showing the whole sequence
                enter_input();
                return;
            }
            // Light up + tone
            uint8_t key = sequence[show_idx];
            led_set_led_color(key, key_colors[key]);
            piezo_play_tone(key_freqs[key], SHOW_TONE_MS);
            show_tone_on = true;
            phase_tick = now;
        }
    } else {
        // Tone is on; wait for it to finish
        if (elapsed >= SHOW_TONE_MS) {
            // Turn off LED, enter gap
            uint8_t key = sequence[show_idx];
            led_set_led_color(key, LED_COLOR_OFF);
            piezo_stop();
            show_idx++;
            show_tone_on = false;
            phase_tick = now;
        }
    }
}

static void update_input(void)
{
    TickType_t now = xTaskGetTickCount();

    // Handle input flash fade-back
    if (input_flash_active) {
        uint32_t flash_elapsed = ((now - input_flash_tick) * 1000) / configTICK_RATE_HZ;
        if (flash_elapsed >= INPUT_FLASH_MS) {
            led_set_led_color(input_flash_key, LED_COLOR_OFF);
            input_flash_active = false;

            if (round_complete) {
                round_complete = false;
                enter_success();
                return;
            }
        }
    }

    // Check timeout
    uint32_t idle_ms = ((now - phase_tick) * 1000) / configTICK_RATE_HZ;
    if (idle_ms >= INPUT_TIMEOUT_MS) {
        ESP_LOGW(TAG, "Input timeout");
        enter_game_over();
    }
}

static void update_success(void)
{
    TickType_t now = xTaskGetTickCount();
    uint32_t elapsed = ((now - phase_tick) * 1000) / configTICK_RATE_HZ;

    if (elapsed >= SUCCESS_DISPLAY_MS) {
        append_random_element();
        enter_showing();
    }
}

static void update_game_over(void)
{
    TickType_t now = xTaskGetTickCount();
    uint32_t elapsed = ((now - phase_tick) * 1000) / configTICK_RATE_HZ;

    if (!go_score_shown) {
        // Red flash animation
        if (go_flash_count < GAMEOVER_FLASHES) {
            if (go_flash_on) {
                led_set_intensity(1.0f);
                led_set_color(LED_COLOR_RED);
                if (elapsed >= GAMEOVER_FLASH_MS) {
                    go_flash_on = false;
                    all_leds_off();
                    phase_tick = now;
                }
            } else {
                if (elapsed >= GAMEOVER_FLASH_MS) {
                    go_flash_count++;
                    go_flash_on = true;
                    phase_tick = now;
                }
            }
        } else {
            // Flashing done, show score and hold
            show_score_leds(score);
            go_score_shown = true;
        }
    }
    // Holding score display; waiting for button press (handled in handle_button)
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void memory_game_init(void)
{
    state = MEM_IDLE;
    seq_len = 0;
    score = 0;
}

void memory_game_enter(void)
{
    state = MEM_IDLE;
    seq_len = 0;
    score = 0;
    show_idle_leds();
    ESP_LOGI(TAG, "Memory game mode active -- press any key to start");
}

void memory_game_handle_button(uint8_t button_id, bool is_long_press)
{
    if (is_long_press || button_id >= NUM_BUTTONS) {
        return;
    }

    switch (state) {
        case MEM_IDLE:
            start_new_game();
            break;

        case MEM_INPUT: {
            // Reset the inactivity timer
            phase_tick = xTaskGetTickCount();

            uint8_t expected = sequence[input_idx];
            if (button_id == expected) {
                // Correct!
                led_set_led_color(button_id, key_colors[button_id]);
                piezo_play_tone(key_freqs[button_id], INPUT_FLASH_MS);
                input_flash_active = true;
                input_flash_key = button_id;
                input_flash_tick = xTaskGetTickCount();

                input_idx++;
                if (input_idx >= seq_len) {
                    round_complete = true;
                }
            } else {
                // Wrong key
                ESP_LOGW(TAG, "Wrong key: got %d, expected %d", button_id, expected);
                enter_game_over();
            }
            break;
        }

        case MEM_GAME_OVER:
            memory_game_enter();
            break;

        default:
            // SHOWING / SUCCESS -- ignore presses
            break;
    }
}

void memory_game_update(void)
{
    switch (state) {
        case MEM_IDLE:
            break;
        case MEM_SHOWING:
            update_showing();
            break;
        case MEM_INPUT:
            update_input();
            break;
        case MEM_SUCCESS:
            update_success();
            break;
        case MEM_GAME_OVER:
            update_game_over();
            break;
    }
}

void memory_game_reset(void)
{
    piezo_stop();
    state = MEM_IDLE;
    seq_len = 0;
    score = 0;
    input_flash_active = false;
    round_complete = false;
}
