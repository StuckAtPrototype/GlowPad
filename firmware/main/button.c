/**
 * @file button.c
 * @brief Capacitive touch button implementation for Pomodoro timer
 * 
 * Uses ESP32-S3 touch sensor v2 driver (esp_driver_touch_sens) to detect
 * touch/release on 5 capacitive pads. Provides short and long press
 * detection with the same callback interface as the original GPIO buttons.
 * 
 * @author StuckAtPrototype, LLC
 * @version 3.0
 */

#include "button.h"
#include "driver/touch_sens.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "button";

// Touch channel IDs mapped to button indices 0-4
static const int touch_channel_ids[NUM_BUTTONS] = {
    TOUCH_CAP1_CHAN,  // Button 0: CAP_1 (5 min)
    TOUCH_CAP2_CHAN,  // Button 1: CAP_2 (10 min)
    TOUCH_CAP3_CHAN,  // Button 2: CAP_3 (25 min)
    TOUCH_CAP4_CHAN,  // Button 3: CAP_4 (45 min)
    TOUCH_CAP5_CHAN,  // Button 4: CAP_5 (60 min)
};

// Threshold-to-benchmark ratio for active detection.
// Higher = less sensitive (needs a firmer touch).
// 0.02 (2%) was too sensitive on bare PCB traces; 0.05 (5%) is a safe starting point.
#define TOUCH_THRESH_RATIO  0.05f

// Number of initial scans for calibration -- more scans = more stable benchmark
#define INIT_SCAN_TIMES  10

// Long press threshold (in milliseconds)
#define LONG_PRESS_MS 500

// Debounce: minimum time between state changes (ms)
#define DEBOUNCE_MS 50

// Touch event types for internal queue
typedef enum {
    TOUCH_EVT_ACTIVE = 0,
    TOUCH_EVT_INACTIVE = 1,
} touch_evt_type_t;

typedef struct {
    int chan_id;
    touch_evt_type_t type;
} touch_event_t;

// Button state tracking
typedef struct {
    bool pressed;
    TickType_t press_start_time;
    TickType_t last_event_time;
    bool long_press_sent;
} button_state_t;

// Module state
static touch_sensor_handle_t s_sens_handle = NULL;
static touch_channel_handle_t s_chan_handles[NUM_BUTTONS] = {0};
static button_state_t s_button_states[NUM_BUTTONS] = {0};
static button_event_callback_t s_event_callback = NULL;
static QueueHandle_t s_touch_evt_queue = NULL;

/**
 * @brief Find button index by touch channel ID
 * @return Button index (0-4) or -1 if not found
 */
static int find_button_index(int chan_id)
{
    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (touch_channel_ids[i] == chan_id) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief ISR callback when a touch channel becomes active
 */
static bool touch_on_active_cb(touch_sensor_handle_t sens_handle, const touch_active_event_data_t *event, void *user_ctx)
{
    touch_event_t evt = {
        .chan_id = event->chan_id,
        .type = TOUCH_EVT_ACTIVE,
    };
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(s_touch_evt_queue, &evt, &xHigherPriorityTaskWoken);
    return (xHigherPriorityTaskWoken == pdTRUE);
}

/**
 * @brief ISR callback when a touch channel becomes inactive
 */
static bool touch_on_inactive_cb(touch_sensor_handle_t sens_handle, const touch_inactive_event_data_t *event, void *user_ctx)
{
    touch_event_t evt = {
        .chan_id = event->chan_id,
        .type = TOUCH_EVT_INACTIVE,
    };
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(s_touch_evt_queue, &evt, &xHigherPriorityTaskWoken);
    return (xHigherPriorityTaskWoken == pdTRUE);
}

/**
 * @brief Task that processes touch events and detects short/long presses
 */
static void button_task(void *pvParameters)
{
    touch_event_t evt;
    ESP_LOGI(TAG, "Touch button task started");

    while (1) {
        if (xQueueReceive(s_touch_evt_queue, &evt, pdMS_TO_TICKS(50))) {
            int btn_idx = find_button_index(evt.chan_id);
            if (btn_idx < 0) {
                continue;
            }

            TickType_t now = xTaskGetTickCount();
            button_state_t *btn = &s_button_states[btn_idx];

            // Debounce
            if ((now - btn->last_event_time) < pdMS_TO_TICKS(DEBOUNCE_MS)) {
                continue;
            }
            btn->last_event_time = now;

            if (evt.type == TOUCH_EVT_ACTIVE) {
                btn->pressed = true;
                btn->press_start_time = now;
                btn->long_press_sent = false;
                ESP_LOGD(TAG, "Touch %d active", btn_idx);
            } else {
                // Release
                if (btn->pressed && !btn->long_press_sent) {
                    uint32_t duration_ms = ((now - btn->press_start_time) * 1000) / configTICK_RATE_HZ;
                    button_press_type_t type = (duration_ms >= LONG_PRESS_MS)
                                               ? BUTTON_PRESS_LONG : BUTTON_PRESS_SHORT;
                    ESP_LOGI(TAG, "Touch %d %s press (%lu ms)", btn_idx,
                             type == BUTTON_PRESS_LONG ? "long" : "short", duration_ms);
                    if (s_event_callback) {
                        s_event_callback(btn_idx, type);
                    }
                }
                btn->pressed = false;
                btn->long_press_sent = false;
            }
        }

        // Check for long presses on held buttons
        TickType_t now = xTaskGetTickCount();
        for (int i = 0; i < NUM_BUTTONS; i++) {
            button_state_t *btn = &s_button_states[i];
            if (btn->pressed && !btn->long_press_sent) {
                uint32_t duration_ms = ((now - btn->press_start_time) * 1000) / configTICK_RATE_HZ;
                if (duration_ms >= LONG_PRESS_MS) {
                    ESP_LOGI(TAG, "Touch %d long press detected (%lu ms)", i, duration_ms);
                    if (s_event_callback) {
                        s_event_callback(i, BUTTON_PRESS_LONG);
                    }
                    btn->long_press_sent = true;
                }
            }
        }
    }
}

/**
 * @brief Perform initial scanning to calibrate touch thresholds
 */
static void touch_do_initial_calibration(void)
{
    ESP_ERROR_CHECK(touch_sensor_enable(s_sens_handle));

    for (int i = 0; i < INIT_SCAN_TIMES; i++) {
        ESP_ERROR_CHECK(touch_sensor_trigger_oneshot_scanning(s_sens_handle, 2000));
    }

    ESP_ERROR_CHECK(touch_sensor_disable(s_sens_handle));

    for (int i = 0; i < NUM_BUTTONS; i++) {
        uint32_t benchmark[TOUCH_SAMPLE_CFG_NUM] = {};
        ESP_ERROR_CHECK(touch_channel_read_data(s_chan_handles[i], TOUCH_CHAN_DATA_TYPE_BENCHMARK, benchmark));

        touch_channel_config_t chan_cfg = {
            .active_thresh = {0},
            .charge_speed = TOUCH_CHARGE_SPEED_7,
            .init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT,
        };
        for (int j = 0; j < TOUCH_SAMPLE_CFG_NUM; j++) {
            chan_cfg.active_thresh[j] = (uint32_t)(benchmark[j] * TOUCH_THRESH_RATIO);
        }
        ESP_ERROR_CHECK(touch_sensor_reconfig_channel(s_chan_handles[i], &chan_cfg));
        ESP_LOGI(TAG, "Button %d (CH %d) benchmark=%lu, thresh=%lu",
                 i, touch_channel_ids[i], benchmark[0], chan_cfg.active_thresh[0]);
    }
}

void button_init(void)
{
    ESP_LOGI(TAG, "Initializing %d capacitive touch buttons", NUM_BUTTONS);

    s_touch_evt_queue = xQueueCreate(20, sizeof(touch_event_t));
    if (s_touch_evt_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create touch event queue");
        return;
    }

    // Step 1: Create touch sensor controller
    touch_sensor_sample_config_t sample_cfg[TOUCH_SAMPLE_CFG_NUM] = {
        TOUCH_SENSOR_V2_DEFAULT_SAMPLE_CONFIG(500, TOUCH_VOLT_LIM_L_0V5, TOUCH_VOLT_LIM_H_2V2)
    };
    touch_sensor_config_t sens_cfg = TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(TOUCH_SAMPLE_CFG_NUM, sample_cfg);
    ESP_ERROR_CHECK(touch_sensor_new_controller(&sens_cfg, &s_sens_handle));

    // Step 2: Create touch channels with initial estimated thresholds
    touch_channel_config_t chan_cfg = {
        .active_thresh = {2000},
        .charge_speed = TOUCH_CHARGE_SPEED_7,
        .init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT,
    };
    for (int i = 0; i < NUM_BUTTONS; i++) {
        ESP_ERROR_CHECK(touch_sensor_new_channel(s_sens_handle, touch_channel_ids[i], &chan_cfg, &s_chan_handles[i]));

        touch_chan_info_t info = {};
        ESP_ERROR_CHECK(touch_sensor_get_channel_info(s_chan_handles[i], &info));
        ESP_LOGI(TAG, "Button %d -> Touch CH %d (GPIO%d)", i, touch_channel_ids[i], info.chan_gpio);
    }

    // Step 3: Configure filter with stronger smoothing and debounce
    touch_sensor_filter_config_t filter_cfg = TOUCH_SENSOR_DEFAULT_FILTER_CONFIG();
    filter_cfg.data.debounce_cnt = 5;
    filter_cfg.data.active_hysteresis = 3;
    filter_cfg.benchmark.filter_mode = TOUCH_BM_IIR_FILTER_16;
    ESP_ERROR_CHECK(touch_sensor_config_filter(s_sens_handle, &filter_cfg));

    // Step 4: Initial calibration
    touch_do_initial_calibration();

    // Step 5: Register active/inactive callbacks
    touch_event_callbacks_t callbacks = {
        .on_active = touch_on_active_cb,
        .on_inactive = touch_on_inactive_cb,
    };
    ESP_ERROR_CHECK(touch_sensor_register_callbacks(s_sens_handle, &callbacks, NULL));

    // Step 6: Enable and start continuous scanning
    ESP_ERROR_CHECK(touch_sensor_enable(s_sens_handle));
    ESP_ERROR_CHECK(touch_sensor_start_continuous_scanning(s_sens_handle));

    // Let the sensor run for a bit so the benchmark stabilises before we process events
    vTaskDelay(pdMS_TO_TICKS(200));

    // Reset benchmarks now that the sensor is warm
    for (int i = 0; i < NUM_BUTTONS; i++) {
        touch_chan_benchmark_config_t bm_cfg = { .do_reset = true };
        ESP_ERROR_CHECK(touch_channel_config_benchmark(s_chan_handles[i], &bm_cfg));
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    // Step 7: Create button processing task
    BaseType_t ret = xTaskCreate(button_task, "button_task", 4096, NULL, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create button task");
        return;
    }

    for (int i = 0; i < NUM_BUTTONS; i++) {
        s_button_states[i].pressed = false;
        s_button_states[i].press_start_time = 0;
        s_button_states[i].last_event_time = 0;
        s_button_states[i].long_press_sent = false;
    }

    ESP_LOGI(TAG, "Capacitive touch button system initialized");
}

void button_register_callback(button_event_callback_t callback)
{
    s_event_callback = callback;
    ESP_LOGI(TAG, "Button event callback registered");
}
