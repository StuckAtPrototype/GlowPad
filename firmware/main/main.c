#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "nvs_flash.h"
#include "led.h"
#include "button.h"
#include "timer.h"
#include "piezo.h"
#include "mode_manager.h"

static const char *TAG = "main";
static TaskHandle_t main_task_handle = NULL;

static void button_event_handler(uint8_t button_id, button_press_type_t press_type)
{
    bool is_long_press = (press_type == BUTTON_PRESS_LONG);
    mode_manager_handle_button(button_id, is_long_press);

    if (main_task_handle != NULL) {
        xTaskNotifyGive(main_task_handle);
    }
}

void app_main(void)
{
    main_task_handle = xTaskGetCurrentTaskHandle();

    ESP_LOGI(TAG, "GlowPad Starting");

    // Configure power management
    esp_pm_config_t pm_config = {
        .max_freq_mhz = 80,
        .min_freq_mhz = 80,
        .light_sleep_enable = false
    };
    esp_err_t ret = esp_pm_configure(&pm_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure power management: %s", esp_err_to_name(ret));
    }

    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Peripherals
    led_init();
    if (piezo_init() == 0) {
        piezo_play_startup_jingle();
    }
    button_init();
    button_register_callback(button_event_handler);
    timer_init();

    // Mode manager (starts in Pomodoro)
    mode_manager_init();
    led_clear_all();

    ESP_LOGI(TAG, "GlowPad Ready");

    // Main loop
    while (1) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
        mode_manager_update();
    }
}
