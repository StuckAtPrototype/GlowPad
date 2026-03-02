/**
 * @file mode_manager.c
 * @brief Application mode manager implementation
 *
 * Handles switching between Pomodoro, Piano, and Memory Game modes.
 * Contains the Pomodoro LED state machine (moved from main.c) and the
 * Memory-mode stub.  Piano mode is delegated to piano.h/c.
 */

#include "mode_manager.h"
#include "timer.h"
#include "piano.h"
#include "led.h"
#include "piezo.h"
#include "button.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "mode_mgr";

static app_mode_t current_mode = MODE_POMODORO;

// Pomodoro LED state machine -- tracks previous timer state for change logging
static timer_state_t last_timer_state = TIMER_STATE_IDLE;
static TickType_t last_jingle_time = 0;

/* ------------------------------------------------------------------ */
/*  Mode-switch helpers                                                */
/* ------------------------------------------------------------------ */

static void leave_current_mode(void)
{
    switch (current_mode) {
        case MODE_POMODORO:
            timer_reset();
            last_timer_state = TIMER_STATE_IDLE;
            last_jingle_time = 0;
            break;
        case MODE_PIANO:
            piano_reset();
            break;
        case MODE_MEMORY:
            break;
        default:
            break;
    }
    piezo_stop();
    led_clear_all();
}

static void play_mode_switch_tone(void)
{
    piezo_play_tone(1200, 60);
    vTaskDelay(pdMS_TO_TICKS(80));
    piezo_play_tone(1600, 60);
    vTaskDelay(pdMS_TO_TICKS(80));
}

static void enter_mode(app_mode_t mode)
{
    current_mode = mode;

    switch (mode) {
        case MODE_POMODORO:
            ESP_LOGI(TAG, "Entering POMODORO mode");
            led_set_intensity(0.3f);
            led_set_color(LED_COLOR_CYAN);
            break;
        case MODE_PIANO:
            ESP_LOGI(TAG, "Entering PIANO mode");
            piano_enter();
            break;
        case MODE_MEMORY:
            ESP_LOGI(TAG, "Entering MEMORY mode (stub)");
            led_set_intensity(0.3f);
            led_set_color(LED_COLOR_YELLOW);
            break;
        default:
            break;
    }
}

static void switch_mode(app_mode_t new_mode)
{
    if (new_mode == current_mode) {
        return;
    }
    leave_current_mode();
    play_mode_switch_tone();
    enter_mode(new_mode);
}

/* ------------------------------------------------------------------ */
/*  Pomodoro LED state machine (moved verbatim from main.c)            */
/* ------------------------------------------------------------------ */

static void pomodoro_update(void)
{
    timer_update();

    timer_state_t ts = timer_get_state();

    if (ts != last_timer_state) {
        ESP_LOGI(TAG, "Timer state changed: %d -> %d", last_timer_state, ts);
        last_timer_state = ts;
    }

    switch (ts) {
        case TIMER_STATE_IDLE:
            led_set_intensity(0.3f);
            led_set_color(LED_COLOR_CYAN);
            if (piezo_is_playing()) {
                piezo_stop();
            }
            break;

        case TIMER_STATE_RUNNING: {
            led_set_intensity(1.0f);
            float progress = timer_get_progress();
            led_set_progress(progress, LED_COLOR_GREEN);
            if (piezo_is_playing()) {
                piezo_stop();
            }
            break;
        }

        case TIMER_STATE_COMPLETED:
            led_set_intensity(1.0f);
            led_set_pulsing(LED_COLOR_GREEN, true);
            if (piezo_is_playing()) {
                piezo_stop();
            }
            break;

        case TIMER_STATE_GRACE_PERIOD:
            led_set_intensity(1.0f);
            led_set_pulsing(LED_COLOR_GREEN, true);
            if (piezo_is_playing()) {
                piezo_stop();
            }
            break;

        case TIMER_STATE_ALERTING: {
            led_set_intensity(1.0f);
            led_set_pulsing(LED_COLOR_RED, true);

            TickType_t now = xTaskGetTickCount();
            if (last_jingle_time == 0 ||
                (now - last_jingle_time > pdMS_TO_TICKS(2000))) {
                if (!piezo_is_playing()) {
                    piezo_play_startup_jingle();
                    last_jingle_time = xTaskGetTickCount();
                }
            }
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void mode_manager_init(void)
{
    current_mode = MODE_POMODORO;
    last_timer_state = TIMER_STATE_IDLE;
    last_jingle_time = 0;
    piano_init();
    ESP_LOGI(TAG, "Mode manager initialised (POMODORO)");
}

void mode_manager_handle_button(uint8_t button_id, bool is_long_press)
{
    // Long press on keys 0-2 triggers a mode switch
    if (is_long_press && button_id < MODE_COUNT) {
        app_mode_t target = (app_mode_t)button_id;
        switch_mode(target);
        return;
    }

    // Forward everything else to the active mode
    switch (current_mode) {
        case MODE_POMODORO:
            timer_handle_button(button_id, is_long_press);
            break;
        case MODE_PIANO:
            piano_handle_button(button_id, is_long_press);
            break;
        case MODE_MEMORY:
            // stub -- ignore
            break;
        default:
            break;
    }
}

void mode_manager_update(void)
{
    switch (current_mode) {
        case MODE_POMODORO:
            pomodoro_update();
            break;
        case MODE_PIANO:
            piano_update();
            break;
        case MODE_MEMORY:
            led_set_intensity(0.3f);
            led_set_color(LED_COLOR_YELLOW);
            break;
        default:
            break;
    }
}

app_mode_t mode_manager_get_mode(void)
{
    return current_mode;
}
