/**
 * @file media_control.c
 * @brief Media control mode – USB HID Consumer (media keys)
 *
 * Maps the five pads to: Previous track, Play/Pause, Next track,
 * Volume Down, Volume Up. Uses HID Consumer Control usage page.
 * Connect GlowPad via native USB (GPIO 19/20 on ESP32-S3) to control
 * music players on the host.
 */

#include "media_control.h"
#include "led.h"
#include "button.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if defined(CONFIG_TINYUSB_HID_COUNT) && (CONFIG_TINYUSB_HID_COUNT > 0)
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_console.h"
#include "tusb.h"
#endif

static const char *TAG = "media_ctrl";

#define NUM_KEYS 5

/* Key mapping: HID Consumer usages (HID Usage Tables) */
#define CONSUMER_SCAN_PREVIOUS  0x00B6u
#define CONSUMER_PLAY_PAUSE     0x00CDu
#define CONSUMER_SCAN_NEXT      0x00B5u
#define CONSUMER_VOLUME_DEC     0x00EAu
#define CONSUMER_VOLUME_INC     0x00E9u

static const uint16_t key_to_usage[NUM_KEYS] = {
    CONSUMER_SCAN_PREVIOUS,   /* Key 0: Previous track */
    CONSUMER_PLAY_PAUSE,      /* Key 1: Play/Pause */
    CONSUMER_SCAN_NEXT,       /* Key 2: Next track */
    CONSUMER_VOLUME_DEC,      /* Key 3: Volume down */
    CONSUMER_VOLUME_INC,      /* Key 4: Volume up */
};

/* Idle: bright blue (GRB) to clearly indicate Media mode */
#define MEDIA_IDLE_COLOR  LED_COLOR_BLUE

/* Per-key highlight colours (GRB) when pressed */
static const uint32_t key_colors_bright[NUM_KEYS] = {
    0x00FF00,
    0xFFFF00,
    0xFF0000,
    0x00FF00,
    0xFF0000,
};

/* Press feedback: timestamp per key (0 = not pressed) */
static TickType_t key_press_tick[NUM_KEYS] = {0};
#define PRESS_FEEDBACK_MS  150
static bool s_usb_stack_ready = false;

#if defined(CONFIG_TINYUSB_HID_COUNT) && (CONFIG_TINYUSB_HID_COUNT > 0)

static void tinyusb_event_handler(tinyusb_event_t *event, void *arg)
{
    (void)arg;
    if (event == NULL) {
        return;
    }

    if (event->id == TINYUSB_EVENT_ATTACHED) {
        ESP_LOGI(TAG, "USB attached/configured by host");
    } else if (event->id == TINYUSB_EVENT_DETACHED) {
        ESP_LOGI(TAG, "USB detached");
    }
}

/* HID Report descriptor: Consumer Control, one 16-bit usage (Report ID 1) */
static const uint8_t consumer_report_descriptor[] = {
    0x05, 0x0C,       /* Usage Page (Consumer) */
    0x09, 0x01,       /* Usage (Consumer Control) */
    0xA1, 0x01,       /* Collection (Application) */
    0x85, 0x01,       /*   Report ID (1) */
    0x15, 0x00,       /*   Logical Minimum (0) */
    0x26, 0xFF, 0x03, /*   Logical Maximum (0x03FF) */
    0x19, 0x00,       /*   Usage Minimum (0) */
    0x2A, 0xFF, 0x03, /*   Usage Maximum (0x03FF) */
    0x95, 0x01,       /*   Report Count (1) */
    0x75, 0x10,       /*   Report Size (16) */
    0x81, 0x00,       /*   Input (Data, Array, Abs) */
    0xC0              /* End Collection */
};

#define CONSUMER_REPORT_LEN  (sizeof(consumer_report_descriptor))

enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_HID,
    ITF_NUM_TOTAL
};

#define EPNUM_CDC_NOTIF   0x81
#define EPNUM_CDC_OUT     0x02
#define EPNUM_CDC_IN      0x82
#define EPNUM_HID_IN      0x83

#define USB_DESC_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_HID_DESC_LEN)

/* Full-speed configuration descriptor: CDC (serial) + HID (media keys) */
static const uint8_t hid_config_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, USB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 0, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, (TUD_OPT_HIGH_SPEED ? 512 : 64)),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, false, CONSUMER_REPORT_LEN, EPNUM_HID_IN, 16, 10),
};

/* TinyUSB callback: return our HID report descriptor */
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return consumer_report_descriptor;
}

/* TinyUSB requires these callbacks when HID class is enabled. */
uint16_t tud_hid_get_report_cb(uint8_t instance,
                               uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer,
                               uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance,
                           uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer,
                           uint16_t bufsize)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

static void send_consumer_key(uint16_t usage)
{
    if (!tud_hid_ready()) {
        return;
    }
    uint8_t report[2] = { (uint8_t)(usage & 0xFF), (uint8_t)(usage >> 8) };
    tud_hid_n_report(0, 1, report, sizeof(report));
}

static void release_consumer_key(void)
{
    if (!tud_hid_ready()) {
        return;
    }
    uint8_t report[2] = { 0x00, 0x00 };
    tud_hid_n_report(0, 1, report, sizeof(report));
}

#else

static void send_consumer_key(uint16_t usage)
{
    (void)usage;
}

static void release_consumer_key(void)
{
}

#endif

static void set_idle_leds(void)
{
    led_set_intensity(0.3f);
    for (int i = 0; i < NUM_KEYS; i++) {
        led_set_led_color((uint8_t)i, MEDIA_IDLE_COLOR);
    }
}

void media_control_init(void)
{
    for (int i = 0; i < NUM_KEYS; i++) {
        key_press_tick[i] = 0;
    }
    s_usb_stack_ready = false;
}

void media_control_enter(void)
{
    for (int i = 0; i < NUM_KEYS; i++) {
        key_press_tick[i] = 0;
    }

#if defined(CONFIG_TINYUSB_HID_COUNT) && (CONFIG_TINYUSB_HID_COUNT > 0)
    if (!s_usb_stack_ready) {
        tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(tinyusb_event_handler);
        tusb_cfg.descriptor.full_speed_config = hid_config_descriptor;
        esp_err_t ret = tinyusb_driver_install(&tusb_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "TinyUSB install failed: %s", esp_err_to_name(ret));
        } else {
            tinyusb_config_cdcacm_t cdc_cfg = {
                .cdc_port = TINYUSB_CDC_ACM_0,
                .callback_rx = NULL,
                .callback_rx_wanted_char = NULL,
                .callback_line_state_changed = NULL,
                .callback_line_coding_changed = NULL,
            };
            esp_err_t cdc_ret = tinyusb_cdcacm_init(&cdc_cfg);
            if (cdc_ret != ESP_OK) {
                ESP_LOGW(TAG, "USB CDC init failed: %s", esp_err_to_name(cdc_ret));
            } else {
                esp_err_t console_ret = tinyusb_console_init(TINYUSB_CDC_ACM_0);
                if (console_ret != ESP_OK) {
                    ESP_LOGW(TAG, "USB console redirect failed: %s", esp_err_to_name(console_ret));
                }
                ESP_LOGI(TAG, "USB CDC serial interface initialised");
            }
            ESP_LOGI(TAG, "USB HID (Consumer) initialised");
            s_usb_stack_ready = true;
        }
    }
#else
    ESP_LOGW(TAG, "Media mode: USB HID not enabled (set CONFIG_TINYUSB_HID_COUNT=1)");
#endif

    set_idle_leds();
    ESP_LOGI(TAG, "Media control mode active");
}

void media_control_handle_button(uint8_t button_id, bool is_long_press)
{
    if (is_long_press || button_id >= NUM_KEYS) {
        return;
    }

    uint16_t usage = key_to_usage[button_id];

    /* Press: send usage */
    send_consumer_key(usage);
    vTaskDelay(pdMS_TO_TICKS(20));
    release_consumer_key();

    /* LED feedback */
    led_set_led_color(button_id, key_colors_bright[button_id]);
    key_press_tick[button_id] = xTaskGetTickCount();
}

void media_control_update(void)
{
    TickType_t now = xTaskGetTickCount();
    for (int i = 0; i < NUM_KEYS; i++) {
        if (key_press_tick[i] != 0) {
            uint32_t elapsed_ms = ((now - key_press_tick[i]) * 1000) / configTICK_RATE_HZ;
            if (elapsed_ms >= PRESS_FEEDBACK_MS) {
                led_set_led_color((uint8_t)i, MEDIA_IDLE_COLOR);
                key_press_tick[i] = 0;
            }
        }
    }
}

void media_control_reset(void)
{
    for (int i = 0; i < NUM_KEYS; i++) {
        key_press_tick[i] = 0;
    }
}
