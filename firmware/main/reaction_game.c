/**
 * @file reaction_game.c
 * @brief Reaction speed game implementation
 *
 * State machine: IDLE -> WAITING -> ACTIVE -> RESULT -> ... -> FINAL -> IDLE
 *                         \-> FALSE_START -/
 *
 * Each key has a colour and tone (same mapping as piano/memory):
 *   Key 0: Red   + C5 (523 Hz)
 *   Key 1: Yellow + D5 (587 Hz)
 *   Key 2: Green  + E5 (659 Hz)
 *   Key 3: Cyan   + G5 (784 Hz)
 *   Key 4: Blue   + A5 (880 Hz)
 */

#include "reaction_game.h"
#include "led.h"
#include "piezo.h"
#include "button.h"
#include "ws2812_control.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "reaction";

/* ------------------------------------------------------------------ */
/*  Constants                                                          */
/* ------------------------------------------------------------------ */

#define TOTAL_ROUNDS        5
#define WAIT_MIN_MS         500
#define WAIT_RANGE_MS       1000
#define ACTIVE_TIMEOUT_MS   2000
#define RESULT_DISPLAY_MS   500
#define FALSE_START_FLASH_MS 250

#define REACT_AMAZING_MS    150
#define REACT_GREAT_MS      250
#define REACT_GOOD_MS       450
#define REACT_OK_MS         550
#define REACT_SLOW_MS       700

#define FALSE_START_TONE_HZ 500
#define FALSE_START_TONE_MS 100

static const uint32_t key_freqs[NUM_BUTTONS] = {
    523, 587, 659, 784, 880
};

static const uint32_t key_colors[NUM_BUTTONS] = {
    0x00FF00,   // key 0 -- Red   (GRB)
    0xFFFF00,   // key 1 -- Yellow
    0xFF0000,   // key 2 -- Green
    0xFF00FF,   // key 3 -- Cyan
    0x0000FF,   // key 4 -- Blue
};

/* ------------------------------------------------------------------ */
/*  Internal state                                                     */
/* ------------------------------------------------------------------ */

typedef enum {
    REACT_IDLE,
    REACT_WAITING,
    REACT_FALSE_START,
    REACT_ACTIVE,
    REACT_RESULT,
    REACT_FINAL,
} react_state_t;

static react_state_t state = REACT_IDLE;
static uint8_t  current_round  = 0;
static uint8_t  target_key     = 0;       // which key is lit during ACTIVE
static uint32_t wait_delay_ms  = 0;       // random delay for current WAITING phase
static TickType_t phase_tick   = 0;
static TickType_t active_start = 0;       // tick when LED lit up (for measuring reaction)

static uint32_t round_times[TOTAL_ROUNDS];   // reaction time per round (0 = fail)
static bool     round_correct[TOTAL_ROUNDS]; // whether the correct key was pressed
static uint8_t  good_count     = 0;          // rounds with correct + < REACT_GOOD_MS

// FALSE_START sub-state
static bool fs_flash_on = false;

// RESULT sub-state
static bool result_is_fail = false;
static bool result_flash_on = false;

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static void show_idle_leds(void)
{
    led_set_intensity(0.3f);
    led_set_color(LED_COLOR_PURPLE);
}

static void all_leds_off(void)
{
    led_set_intensity(1.0f);
    for (int i = 0; i < NUM_LEDS; i++) {
        led_set_led_color((uint8_t)i, LED_COLOR_OFF);
    }
}

static uint32_t random_delay(void)
{
    return (esp_random() % WAIT_RANGE_MS) + WAIT_MIN_MS;
}

static uint32_t ticks_to_ms(TickType_t ticks)
{
    return (ticks * 1000) / configTICK_RATE_HZ;
}

static void show_reaction_bar(uint32_t reaction_ms)
{
    uint8_t leds;
    uint32_t color;

    if (reaction_ms < REACT_AMAZING_MS) {
        leds = 5; color = LED_COLOR_GREEN;
    } else if (reaction_ms < REACT_GREAT_MS) {
        leds = 4; color = LED_COLOR_GREEN;
    } else if (reaction_ms < REACT_GOOD_MS) {
        leds = 3; color = LED_COLOR_YELLOW;
    } else if (reaction_ms < REACT_OK_MS) {
        leds = 2; color = LED_COLOR_YELLOW;
    } else {
        leds = 1; color = LED_COLOR_RED;
    }

    led_set_intensity(1.0f);
    for (int i = 0; i < NUM_LEDS; i++) {
        led_set_led_color((uint8_t)i, (i < leds) ? color : LED_COLOR_OFF);
    }

    // Higher pitch = faster reaction
    uint32_t tone_freq = 600 + (REACT_SLOW_MS - reaction_ms);
    if (tone_freq < 400) tone_freq = 400;
    if (tone_freq > 1600) tone_freq = 1600;
    piezo_play_tone(tone_freq, 150);
}

static void show_final_score(void)
{
    led_set_intensity(1.0f);
    for (int i = 0; i < NUM_LEDS; i++) {
        led_set_led_color((uint8_t)i, (i < good_count) ? LED_COLOR_GREEN : LED_COLOR_OFF);
    }
}

/* ------------------------------------------------------------------ */
/*  State transitions                                                  */
/* ------------------------------------------------------------------ */

static void enter_waiting(void)
{
    all_leds_off();
    wait_delay_ms = random_delay();
    phase_tick = xTaskGetTickCount();
    state = REACT_WAITING;
    ESP_LOGI(TAG, "Round %d/%d -- waiting (delay %lu ms)",
             current_round + 1, TOTAL_ROUNDS, (unsigned long)wait_delay_ms);
}

static void enter_active(void)
{
    target_key = (uint8_t)(esp_random() % NUM_BUTTONS);
    led_set_intensity(1.0f);
    led_set_led_color(target_key, key_colors[target_key]);
    piezo_play_tone(key_freqs[target_key], 100);
    active_start = xTaskGetTickCount();
    phase_tick = active_start;
    state = REACT_ACTIVE;
}

static void enter_false_start(void)
{
    piezo_play_tone(FALSE_START_TONE_HZ, FALSE_START_TONE_MS);
    led_set_intensity(1.0f);
    led_set_color(LED_COLOR_RED);
    fs_flash_on = true;
    phase_tick = xTaskGetTickCount();
    state = REACT_FALSE_START;
    ESP_LOGW(TAG, "False start!");
}

static void enter_result(bool correct, uint32_t reaction_ms)
{
    round_correct[current_round] = correct;
    round_times[current_round] = correct ? reaction_ms : 0;

    if (correct) {
        ESP_LOGI(TAG, "Round %d: correct! %lu ms", current_round + 1, (unsigned long)reaction_ms);
        led_set_led_color(target_key, LED_COLOR_GREEN);
        piezo_play_tone(1200, 100);
        result_is_fail = false;
    } else {
        ESP_LOGW(TAG, "Round %d: fail", current_round + 1);
        led_set_intensity(1.0f);
        led_set_color(LED_COLOR_RED);
        piezo_play_tone(FALSE_START_TONE_HZ, 150);
        result_is_fail = true;
        result_flash_on = true;
    }

    phase_tick = xTaskGetTickCount();
    state = REACT_RESULT;
}

static void enter_final(void)
{
    good_count = 0;
    uint32_t total_ms = 0;
    for (int i = 0; i < TOTAL_ROUNDS; i++) {
        if (round_correct[i] && round_times[i] < REACT_GOOD_MS) {
            good_count++;
        }
        ESP_LOGI(TAG, "  Round %d: %s  %lu ms",
                 i + 1,
                 round_correct[i] ? "OK" : "FAIL",
                 (unsigned long)round_times[i]);
        if (round_correct[i]) {
            total_ms += round_times[i];
        }
    }

    ESP_LOGI(TAG, "Game complete! Score: %d/%d good (<400ms)  |  Total time: %lu ms",
             good_count, TOTAL_ROUNDS, (unsigned long)total_ms);
    piezo_play_tone(1200, 60);
    vTaskDelay(pdMS_TO_TICKS(80));
    piezo_play_tone(1600, 60);
    vTaskDelay(pdMS_TO_TICKS(80));

    show_final_score();
    state = REACT_FINAL;
}

static void start_new_game(void)
{
    current_round = 0;
    good_count = 0;
    for (int i = 0; i < TOTAL_ROUNDS; i++) {
        round_times[i] = 0;
        round_correct[i] = false;
    }
    ESP_LOGI(TAG, "New game started");
    enter_waiting();
}

/* ------------------------------------------------------------------ */
/*  State-machine update (called every ~100 ms)                        */
/* ------------------------------------------------------------------ */

static void update_waiting(void)
{
    TickType_t now = xTaskGetTickCount();
    uint32_t elapsed = ticks_to_ms(now - phase_tick);

    if (elapsed >= wait_delay_ms) {
        enter_active();
    }
}

static void update_false_start(void)
{
    TickType_t now = xTaskGetTickCount();
    uint32_t elapsed = ticks_to_ms(now - phase_tick);

    if (fs_flash_on && elapsed >= FALSE_START_FLASH_MS) {
        all_leds_off();
        fs_flash_on = false;
        phase_tick = now;
    } else if (!fs_flash_on && elapsed >= FALSE_START_FLASH_MS) {
        enter_waiting();
    }
}

static void update_active(void)
{
    TickType_t now = xTaskGetTickCount();
    uint32_t elapsed = ticks_to_ms(now - phase_tick);

    if (elapsed >= ACTIVE_TIMEOUT_MS) {
        ESP_LOGW(TAG, "Timeout!");
        enter_result(false, 0);
    }
}

static void update_result(void)
{
    TickType_t now = xTaskGetTickCount();
    uint32_t elapsed = ticks_to_ms(now - phase_tick);

    // For fail results, do a quick flash toggle at the halfway point
    if (result_is_fail && result_flash_on && elapsed >= RESULT_DISPLAY_MS / 2) {
        all_leds_off();
        result_flash_on = false;
    }

    if (elapsed >= RESULT_DISPLAY_MS) {
        current_round++;
        if (current_round >= TOTAL_ROUNDS) {
            enter_final();
        } else {
            enter_waiting();
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void reaction_game_init(void)
{
    state = REACT_IDLE;
    current_round = 0;
    good_count = 0;
}

void reaction_game_enter(void)
{
    state = REACT_IDLE;
    current_round = 0;
    good_count = 0;
    show_idle_leds();
    ESP_LOGI(TAG, "Reaction game mode active -- press any key to start");
}

void reaction_game_handle_button(uint8_t button_id, bool is_long_press)
{
    if (is_long_press || button_id >= NUM_BUTTONS) {
        return;
    }

    switch (state) {
        case REACT_IDLE:
            start_new_game();
            break;

        case REACT_WAITING:
            enter_false_start();
            break;

        case REACT_ACTIVE: {
            TickType_t now = xTaskGetTickCount();
            uint32_t reaction_ms = ticks_to_ms(now - active_start);

            if (button_id == target_key) {
                enter_result(true, reaction_ms);
            } else {
                enter_result(false, 0);
            }
            break;
        }

        case REACT_FINAL:
            reaction_game_enter();
            break;

        default:
            break;
    }
}

void reaction_game_update(void)
{
    switch (state) {
        case REACT_IDLE:
            break;
        case REACT_WAITING:
            update_waiting();
            break;
        case REACT_FALSE_START:
            update_false_start();
            break;
        case REACT_ACTIVE:
            update_active();
            break;
        case REACT_RESULT:
            update_result();
            break;
        case REACT_FINAL:
            break;
    }
}

void reaction_game_reset(void)
{
    piezo_stop();
    state = REACT_IDLE;
    current_round = 0;
    good_count = 0;
}
