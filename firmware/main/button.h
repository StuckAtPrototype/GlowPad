/**
 * @file button.h
 * @brief Capacitive touch button interface for Pomodoro timer
 * 
 * Provides the same public API as the original GPIO-button driver,
 * but uses ESP32-S3 capacitive touch pads for input.
 * 
 * @author StuckAtPrototype, LLC
 * @version 3.0
 */

#ifndef BUTTON_H
#define BUTTON_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Touch pad channel IDs (ESP32-S3 touch sensor v2)
#define TOUCH_CAP1_CHAN  4   // GPIO4 / TOUCH_PAD_NUM4 - CAP_1
#define TOUCH_CAP2_CHAN  5   // GPIO5 / TOUCH_PAD_NUM5 - CAP_2
#define TOUCH_CAP3_CHAN  3   // GPIO3 / TOUCH_PAD_NUM3 - CAP_3
#define TOUCH_CAP4_CHAN  2   // GPIO2 / TOUCH_PAD_NUM2 - CAP_4
#define TOUCH_CAP5_CHAN  1   // GPIO1 / TOUCH_PAD_NUM1 - CAP_5

// Button indices (unchanged from original)
#define BUTTON_SW0 0
#define BUTTON_SW1 1
#define BUTTON_SW2 2
#define BUTTON_SW3 3
#define BUTTON_SW4 4
#define NUM_BUTTONS 5

// Button press types
typedef enum {
    BUTTON_PRESS_SHORT = 0,
    BUTTON_PRESS_LONG = 1
} button_press_type_t;

// Button event callback type
typedef void (*button_event_callback_t)(uint8_t button_id, button_press_type_t press_type);

/**
 * @brief Initialize capacitive touch button system
 * 
 * Configures ESP32-S3 touch sensor controller and 5 touch channels,
 * performs initial calibration, and starts continuous scanning.
 */
void button_init(void);

/**
 * @brief Register a callback for button events
 * 
 * @param callback Function to call when a button event occurs
 */
void button_register_callback(button_event_callback_t callback);

#ifdef __cplusplus
}
#endif

#endif // BUTTON_H
