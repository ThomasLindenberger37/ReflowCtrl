#include "wifi_station.hpp"

#include <atomic>
#include <cstring>

#include "Credentials.hpp"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

namespace reflowCtrl {
namespace {

constexpr char TAG[] = "wifi_station";
std::atomic_bool connected{false};

void handle_wifi_event(void*, esp_event_base_t event_base, int32_t event_id, void*) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        connected = false;
        ESP_LOGW(TAG, "Wi-Fi connection lost; reconnecting");
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        connected = true;
        ESP_LOGI(TAG, "Wi-Fi connected");
    }
}

}  // namespace

esp_err_t start_wifi_station() {
    if (credentials::WIFI_SSID[0] == '\0') {
        ESP_LOGE(TAG, "Wi-Fi SSID is empty; configure main/Credentials.hpp");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "Failed to erase NVS");
        result = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(result, TAG, "Failed to initialize NVS");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Failed to initialize network stack");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Failed to create event loop");

    if (esp_netif_create_default_wifi_sta() == nullptr) {
        return ESP_FAIL;
    }

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "Failed to initialize Wi-Fi");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &handle_wifi_event, nullptr), TAG,
        "Failed to register Wi-Fi event handler");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &handle_wifi_event, nullptr), TAG,
        "Failed to register IP event handler");

    wifi_config_t wifi_config{};
    std::strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid), credentials::WIFI_SSID,
                 sizeof(wifi_config.sta.ssid) - 1);
    std::strncpy(reinterpret_cast<char*>(wifi_config.sta.password), credentials::WIFI_PASSWORD,
                 sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Failed to set station mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG,
                        "Failed to configure station");
    return esp_wifi_start();
}

bool is_wifi_connected() {
    return connected.load();
}

}  // namespace reflowCtrl
