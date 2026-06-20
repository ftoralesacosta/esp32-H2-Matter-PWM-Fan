/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_log.h>
#include <stdlib.h>
#include <string.h>

#include <esp_matter.h>
#include <app_priv.h>
#include <common_macros.h>

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <iot_button.h>

using namespace chip::app::Clusters;
using namespace esp_matter;

static const char *TAG = "app_driver";
extern uint16_t fan_endpoint_id;

#define PWM_FAN_GPIO            GPIO_NUM_5        // GPIO 5 as soldered on hardware
#define PWM_LEDC_CHANNEL        LEDC_CHANNEL_0
#define PWM_LEDC_TIMER          LEDC_TIMER_0
#define PWM_LEDC_MODE           LEDC_LOW_SPEED_MODE
#define PWM_LEDC_RESOLUTION     LEDC_TIMER_10_BIT // 10-bit resolution (0 to 1023)
#define PWM_LEDC_FREQ           25000             // 25 kHz PWM frequency for Noctua fan

static uint8_t current_speed_percentage = 0;

static esp_err_t app_driver_fan_set_speed(uint8_t speed_percentage)
{
    if (speed_percentage > 100) {
        speed_percentage = 100;
    }

    // Map speed percentage (0-100) to LEDC duty cycle (0-1023)
    uint32_t duty = (speed_percentage * 1023) / 100;

    esp_err_t err = ledc_set_duty(PWM_LEDC_MODE, PWM_LEDC_CHANNEL, duty);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set LEDC duty: %s", esp_err_to_name(err));
        return err;
    }

    err = ledc_update_duty(PWM_LEDC_MODE, PWM_LEDC_CHANNEL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update LEDC duty: %s", esp_err_to_name(err));
        return err;
    }

    current_speed_percentage = speed_percentage;
    ESP_LOGI(TAG, "Fan speed updated to %d%% (Duty: %ld)", speed_percentage, duty);
    return ESP_OK;
}

static void app_driver_button_toggle_cb(void *arg, void *data)
{
    ESP_LOGI(TAG, "Toggle button pressed");
    uint16_t endpoint_id = fan_endpoint_id;
    uint32_t cluster_id = FanControl::Id;
    uint32_t attribute_id = FanControl::Attributes::PercentSetting::Id;

    attribute_t *attribute = attribute::get(endpoint_id, cluster_id, attribute_id);
    if (!attribute) {
        ESP_LOGE(TAG, "Failed to get PercentSetting attribute");
        return;
    }

    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    attribute::get_val(attribute, &val);

    // Toggle: if running, turn off; if off, turn on at DEFAULT_FAN_SPEED
    if (val.val.u8 > 0) {
        val.val.u8 = 0;
    } else {
        val.val.u8 = DEFAULT_FAN_SPEED;
    }

    attribute::update(endpoint_id, cluster_id, attribute_id, &val);
}

esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle, uint16_t endpoint_id, uint32_t cluster_id,
                                      uint32_t attribute_id, esp_matter_attr_val_t *val)
{
    esp_err_t err = ESP_OK;
    if (endpoint_id == fan_endpoint_id) {
        if (cluster_id == FanControl::Id) {
            if (attribute_id == FanControl::Attributes::PercentSetting::Id) {
                // Matter percent setting is a nullable uint8 (0 to 100)
                uint8_t speed = val->val.u8;
                err = app_driver_fan_set_speed(speed);
            }
        }
    }
    return err;
}

esp_err_t app_driver_attribute_post_update(app_driver_handle_t driver_handle, uint16_t endpoint_id, uint32_t cluster_id,
                                           uint32_t attribute_id, esp_matter_attr_val_t *val)
{
    esp_err_t err = ESP_OK;
    if (endpoint_id == fan_endpoint_id) {
        if (cluster_id == FanControl::Id) {
            if (attribute_id == FanControl::Attributes::PercentSetting::Id) {
                uint8_t speed = val->val.u8;
                
                // Update PercentCurrent to match PercentSetting (now that the database is unlocked)
                esp_matter_attr_val_t current_val = esp_matter_uint8(speed);
                esp_err_t temp_err = attribute::update(endpoint_id, FanControl::Id, FanControl::Attributes::PercentCurrent::Id, &current_val);
                if (temp_err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to update PercentCurrent: %s", esp_err_to_name(temp_err));
                }
                
                // Update FanMode to match PercentSetting (On if speed > 0, Off if speed == 0)
                // In Matter FanControl: 0 is Off, 4 is On (kOn)
                uint8_t mode = (speed > 0) ? 4 : 0;
                esp_matter_attr_val_t mode_val = esp_matter_enum8(mode);
                temp_err = attribute::update(endpoint_id, FanControl::Id, FanControl::Attributes::FanMode::Id, &mode_val);
                if (temp_err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to update FanMode: %s", esp_err_to_name(temp_err));
                }
            }
        }
    }
    return err;
}

esp_err_t app_driver_fan_set_defaults(uint16_t endpoint_id)
{
    esp_err_t err = ESP_OK;
    esp_matter_attr_val_t val = esp_matter_invalid(NULL);

    attribute_t *attribute = attribute::get(endpoint_id, FanControl::Id, FanControl::Attributes::PercentSetting::Id);
    if (attribute) {
        attribute::get_val(attribute, &val);
        err = app_driver_fan_set_speed(val.val.u8);
    }
    return err;
}

app_driver_handle_t app_driver_fan_init()
{
    // Release the pin from JTAG/strapping and route it to the GPIO matrix
    gpio_reset_pin(PWM_FAN_GPIO);

    // 1. Configure LEDC Timer
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = PWM_LEDC_MODE,
        .duty_resolution  = PWM_LEDC_RESOLUTION,
        .timer_num        = PWM_LEDC_TIMER,
        .freq_hz          = PWM_LEDC_FREQ,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    
    esp_err_t err = ledc_timer_config(&ledc_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC timer config failed: %s", esp_err_to_name(err));
        return NULL;
    }

    // 2. Configure LEDC Channel
    ledc_channel_config_t ledc_channel = {
        .gpio_num       = PWM_FAN_GPIO,
        .speed_mode     = PWM_LEDC_MODE,
        .channel        = PWM_LEDC_CHANNEL,
        .intr_type      = LEDC_INTR_DISABLE,
        .timer_sel      = PWM_LEDC_TIMER,
        .duty           = 0,
        .hpoint         = 0
    };

    err = ledc_channel_config(&ledc_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC channel config failed: %s", esp_err_to_name(err));
        return NULL;
    }

    ESP_LOGI(TAG, "Noctua Fan LEDC PWM initialized at 25kHz on GPIO %d", PWM_FAN_GPIO);
    return (app_driver_handle_t)1; // Return non-null handle to indicate success
}

app_driver_handle_t app_driver_button_init()
{
    button_config_t config;
    memset(&config, 0, sizeof(button_config_t));

    config.type = BUTTON_TYPE_GPIO;
    config.gpio_button_config.gpio_num = GPIO_NUM_2; // Changed back to GPIO 2 for consistency with original repo
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
