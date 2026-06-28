/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_ota.h>
#include <esp_matter_providers.h>

#include <common_macros.h>
#include <app_priv.h>
#include <app_reset.h>

#if CONFIG_OPENTHREAD_ENABLED
#include <platform/ESP32/OpenthreadLauncher.h>
#include <openthread/thread.h>
#include "esp_openthread.h"
#endif


#if CONFIG_DYNAMIC_PASSCODE_COMMISSIONABLE_DATA_PROVIDER
#include <custom_provider/dynamic_commissionable_data_provider.h>
#endif

static const char *TAG = "app_main";
uint16_t fan_endpoint_id = 0;

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

#if CONFIG_DYNAMIC_PASSCODE_COMMISSIONABLE_DATA_PROVIDER
dynamic_commissionable_data_provider g_dynamic_passcode_provider;
#endif

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "Interface IP Address Changed");
#if CONFIG_OPENTHREAD_ENABLED
        {
            otInstance *instance = esp_openthread_get_instance();
            if (instance) {
                for (const otNetifAddress *addr = otIp6GetUnicastAddresses(instance); addr; addr = addr->mNext) {
                    char buf[40];
                    otIp6AddressToString(&addr->mAddress, buf, sizeof(buf));
                    ESP_LOGI(TAG, "  OT Addr: %s", buf);
                }
            }
        }
#endif
        break;


    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed, fail safe timer expired");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
        ESP_LOGI(TAG, "Commissioning session started");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped:
        ESP_LOGI(TAG, "Commissioning session stopped");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning window opened");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning window closed");
        break;

    default:
        break;
    }
}

// This callback is invoked when clients interact with the Identify Cluster.
// In the callback implementation, an endpoint can identify itself. (e.g., by flashing an LED or light).
static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: type: %u, effect: %u, variant: %u", type, effect_id, effect_variant);
    return ESP_OK;
}

// This callback is called for every attribute update. The callback implementation shall
// handle the desired attributes and return an appropriate error code. If the attribute
// is not of your interest, please do not return an error code and strictly return ESP_OK.
static esp_err_t app_attribute_update_cb(callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    app_driver_handle_t driver_handle = (app_driver_handle_t)priv_data;

    if (type == PRE_UPDATE) {
        /* Handle the attribute updates here. */
        return app_driver_attribute_update(driver_handle, endpoint_id, cluster_id, attribute_id, val);
    } else if (type == POST_UPDATE) {
        /* Handle post-update status updates here. */
        return app_driver_attribute_post_update(driver_handle, endpoint_id, cluster_id, attribute_id, val);
    }

    return ESP_OK;
}

extern "C" void app_main()
{
    esp_err_t err = ESP_OK;

    /* Initialize the ESP NVS layer */
    nvs_flash_init();

    /* Initialize driver */
    app_driver_handle_t fan_handle = app_driver_fan_init();
    app_driver_handle_t button_handle = app_driver_button_init();
    app_reset_button_register(button_handle);

    /* Create a Matter node and add the mandatory Root Node device type on endpoint 0 */
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

    // Configure the Matter Fan endpoint
    fan::config_t fan_config;
    fan_config.fan_control.fan_mode = 0; // Off
    fan_config.fan_control.percent_setting = DEFAULT_FAN_SPEED;
    fan_config.fan_control.percent_current = DEFAULT_FAN_SPEED;

    // Create the Fan device endpoint
    endpoint_t *endpoint = fan::create(node, &fan_config, ENDPOINT_FLAG_NONE, fan_handle);
    ABORT_APP_ON_FAILURE(endpoint != nullptr, ESP_LOGE(TAG, "Failed to create Matter Fan endpoint"));

    fan_endpoint_id = endpoint::get_id(endpoint);
    ESP_LOGI(TAG, "Fan created with endpoint_id %d", fan_endpoint_id);

    /* Mark deferred persistence for some attributes that might be changed rapidly */
    attribute_t *percent_setting_attribute = attribute::get(fan_endpoint_id, FanControl::Id, FanControl::Attributes::PercentSetting::Id);
    attribute::set_deferred_persistence(percent_setting_attribute);

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    /* Set OpenThread platform config */
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&config);
#endif

#if CONFIG_DYNAMIC_PASSCODE_COMMISSIONABLE_DATA_PROVIDER
    /* This should be called before esp_matter::start() */
    esp_matter::set_custom_commissionable_data_provider(&g_dynamic_passcode_provider);
#endif

    /* Matter start */
    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));

    /* Starting driver with default values */
    app_driver_fan_set_defaults(fan_endpoint_id);
}
