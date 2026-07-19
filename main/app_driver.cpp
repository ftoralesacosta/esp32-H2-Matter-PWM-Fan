/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_log.h>
#include <stdlib.h>
#include <string.h>
#include <esp_timer.h>

#include <esp_matter.h>
#include <app_priv.h>
#include <common_macros.h>

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <iot_button.h>

using namespace chip::app::Clusters;
using namespace esp_matter;

static const char *TAG = "app_driver";
extern uint16_t light_endpoint_id;

// Drives a 5V LED through an opto-isolated MOSFET switch module (signal in,
// V+/V- to the LED + external 5V supply). Pin choice differs by board:
#if CONFIG_IDF_TARGET_ESP32H2
// Waveshare ESP32-H2-Zero (pin-compatible with Espressif's ESP32-H2-DevKitM-1).
// GPIO11 is a plain, unshared GPIO. Deliberately NOT using GPIO13/14
// (32.768kHz crystal per Espressif's official DevKitM-1 guide), GPIO23/24
// (UART0 TX/RX - the serial console this whole project's monitor_log.txt
// workflow depends on), or GPIO9 (onboard BOOT button/strapping pin).
#define LED_PWM_GPIO             GPIO_NUM_11
#define BUTTON_GPIO              GPIO_NUM_10
#else
// Seeed Studio XIAO ESP32-C6 (default/original target). Physical pin D3 -
// same pin number the separate fan-controller branch uses, but this is an
// independent build/device, not running alongside a fan on the same chip.
#define LED_PWM_GPIO             GPIO_NUM_21
#define BUTTON_GPIO              GPIO_NUM_2
#endif

#define LED_PWM_LEDC_CHANNEL     LEDC_CHANNEL_0
#define LED_PWM_LEDC_TIMER       LEDC_TIMER_0
#define LED_PWM_LEDC_MODE        LEDC_LOW_SPEED_MODE
#define LED_PWM_LEDC_RESOLUTION  LEDC_TIMER_10_BIT // 10-bit resolution (0 to 1023)
#define LED_PWM_LEDC_FREQ        1000              // 1 kHz - flicker-free, well within
                                                    // the opto-isolator's switching speed

static bool current_on_off = false;
static uint8_t current_level = DEFAULT_LED_LEVEL; // Matter CurrentLevel range: 0-254

// Applies current_on_off/current_level to the physical PWM output. Matter's
// CurrentLevel is a nullable<uint8_t> with a 0-254 range (not 0-100 like
// FanControl's PercentSetting), hence the /254 here.
static esp_err_t app_driver_light_apply()
{
    uint32_t duty = current_on_off ? ((uint32_t)current_level * 1023) / 254 : 0;

    esp_err_t err = ledc_set_duty(LED_PWM_LEDC_MODE, LED_PWM_LEDC_CHANNEL, duty);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set LEDC duty: %s", esp_err_to_name(err));
        return err;
    }

    err = ledc_update_duty(LED_PWM_LEDC_MODE, LED_PWM_LEDC_CHANNEL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update LEDC duty: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "LED updated: on=%d level=%d (Duty: %lu)", current_on_off, current_level, duty);
    return ESP_OK;
}

static void app_driver_button_toggle_cb(void *arg, void *data)
{
    ESP_LOGI(TAG, "Toggle button pressed");
    uint16_t endpoint_id = light_endpoint_id;
    uint32_t cluster_id = OnOff::Id;
    uint32_t attribute_id = OnOff::Attributes::OnOff::Id;

    chip::DeviceLayer::PlatformMgr().LockChipStack();

    attribute_t *attribute = attribute::get(endpoint_id, cluster_id, attribute_id);
    if (!attribute) {
        ESP_LOGE(TAG, "Failed to get OnOff attribute");
        chip::DeviceLayer::PlatformMgr().UnlockChipStack();
        return;
    }

    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    attribute::get_val(attribute, &val);
    val.val.b = !val.val.b;
    attribute::update(endpoint_id, cluster_id, attribute_id, &val);

    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
}

esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle, uint16_t endpoint_id, uint32_t cluster_id,
                                      uint32_t attribute_id, esp_matter_attr_val_t *val)
{
    if (endpoint_id != light_endpoint_id) {
        return ESP_OK;
    }

    // No debounce timer here (unlike the separate fan-controller branch's
    // PercentSetting handling, which exists to work around a FanMode
    // auto-substitution quirk - see that branch's FINDINGS.md). LevelControl's
    // CurrentLevel has no equivalent quirk, so writes are applied directly.
    if (cluster_id == OnOff::Id && attribute_id == OnOff::Attributes::OnOff::Id) {
        current_on_off = val->val.b;
        return app_driver_light_apply();
    }
    if (cluster_id == LevelControl::Id && attribute_id == LevelControl::Attributes::CurrentLevel::Id) {
        current_level = val->val.u8;
        return app_driver_light_apply();
    }
    return ESP_OK;
}

esp_err_t app_driver_attribute_post_update(app_driver_handle_t driver_handle, uint16_t endpoint_id, uint32_t cluster_id,
                                           uint32_t attribute_id, esp_matter_attr_val_t *val)
{
    return ESP_OK;
}

esp_err_t app_driver_light_set_defaults(uint16_t endpoint_id)
{
    esp_matter_attr_val_t val = esp_matter_invalid(NULL);

    attribute_t *on_off_attribute = attribute::get(endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id);
    if (on_off_attribute) {
        attribute::get_val(on_off_attribute, &val);
        current_on_off = val.val.b;
    }

    attribute_t *level_attribute = attribute::get(endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
    if (level_attribute) {
        attribute::get_val(level_attribute, &val);
        current_level = val.val.u8;
    }

    return app_driver_light_apply();
}

app_driver_handle_t app_driver_light_init()
{
    gpio_reset_pin(LED_PWM_GPIO);
    gpio_set_direction(LED_PWM_GPIO, GPIO_MODE_OUTPUT);

    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LED_PWM_LEDC_MODE,
        .duty_resolution  = LED_PWM_LEDC_RESOLUTION,
        .timer_num        = LED_PWM_LEDC_TIMER,
        .freq_hz          = LED_PWM_LEDC_FREQ,
        .clk_cfg          = LEDC_AUTO_CLK
    };

    esp_err_t err = ledc_timer_config(&ledc_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC timer config failed: %s", esp_err_to_name(err));
        return NULL;
    }

    ledc_channel_config_t ledc_channel = {
        .gpio_num       = LED_PWM_GPIO,
        .speed_mode     = LED_PWM_LEDC_MODE,
        .channel        = LED_PWM_LEDC_CHANNEL,
        .intr_type      = LEDC_INTR_DISABLE,
        .timer_sel      = LED_PWM_LEDC_TIMER,
        .duty           = 0,
        .hpoint         = 0
    };

    err = ledc_channel_config(&ledc_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC channel config failed: %s", esp_err_to_name(err));
        return NULL;
    }

    ESP_LOGI(TAG, "LED PWM initialized at 1kHz on GPIO %d", LED_PWM_GPIO);

    return (app_driver_handle_t)1;
}

app_driver_handle_t app_driver_button_init()
{
    button_config_t config;
    memset(&config, 0, sizeof(button_config_t));

    config.type = BUTTON_TYPE_GPIO;
    config.gpio_button_config.gpio_num = BUTTON_GPIO;
    config.gpio_button_config.active_level = 1;      // Active high (1)

    button_handle_t handle = iot_button_create(&config);
    if (!handle) {
        ESP_LOGE(TAG, "Failed to create button device");
        return NULL;
    }

    esp_err_t err = iot_button_register_cb(handle, BUTTON_PRESS_DOWN, app_driver_button_toggle_cb, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register button callback");
        return NULL;
    }

    return (app_driver_handle_t)handle;
}
