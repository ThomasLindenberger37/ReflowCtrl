#include "ota_updater.hpp"

#include <array>
#include <cstdio>

#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "wifi_station.hpp"

namespace reflowCtrl {
namespace {

constexpr char TAG[] = "ota_updater";
constexpr TickType_t CHECK_PERIOD = pdMS_TO_TICKS(5000);
constexpr TickType_t CONFIRM_RETRY_PERIOD = pdMS_TO_TICKS(1000);
constexpr uint32_t TASK_STACK_SIZE = 8192;
constexpr UBaseType_t TASK_PRIORITY = 5;

std::array<char, 192> make_url(const char* host, const char* path) {
    std::array<char, 192> url{};
    std::snprintf(url.data(), url.size(), "http://%s:%d%s", host, CONFIG_REFLOW_OTA_SERVER_PORT,
                  path);
    return url;
}

bool confirm_update_with_host(const char* host) {
    const auto url = make_url(host, "/complete");
    esp_http_client_config_t config{};
    config.url = url.data();
    config.timeout_ms = 3000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return false;
    }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_post_field(client, "", 0);
    const esp_err_t result = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return result == ESP_OK && status == 200;
}

bool confirm_update() {
    constexpr std::array<const char*, 2> hosts = {
        CONFIG_REFLOW_OTA_SERVER_HOSTNAME,
        "reflow_ota_server.local",
    };

    for (const char* host : hosts) {
        if (confirm_update_with_host(host)) {
            return true;
        }
    }

    return false;
}

bool ota_update_with_host(const char* host) {
    const auto firmware_url = make_url(host, "/firmware.bin");

    esp_http_client_config_t http_config{};
    http_config.url = firmware_url.data();
    http_config.timeout_ms = 5000;
    http_config.keep_alive_enable = true;

    esp_https_ota_config_t ota_config{};
    ota_config.http_config = &http_config;

    const esp_err_t result = esp_https_ota(&ota_config);
    if (result == ESP_OK) {
        return true;
    }

    if (result != ESP_ERR_HTTP_CONNECT) {
        ESP_LOGW(TAG, "OTA check failed for %s: %s", host, esp_err_to_name(result));
    }
    return false;
}

void ota_task(void*) {
    while (true) {
        if (!is_wifi_connected()) {
            vTaskDelay(CHECK_PERIOD);
            continue;
        }

        constexpr std::array<const char*, 2> hosts = {
            CONFIG_REFLOW_OTA_SERVER_HOSTNAME,
            "reflow_ota_server.local",
        };

        bool downloaded = false;
        for (const char* host : hosts) {
            if (ota_update_with_host(host)) {
                downloaded = true;
                break;
            }
        }

        if (!downloaded) {
            vTaskDelay(CHECK_PERIOD);
            continue;
        }

        ESP_LOGI(TAG, "Firmware image installed; confirming download to server");
        while (!confirm_update()) {
            ESP_LOGW(TAG, "Could not confirm update; retrying");
            vTaskDelay(CONFIRM_RETRY_PERIOD);
        }

        ESP_LOGI(TAG, "Update confirmed; restarting");
        esp_restart();
    }
}

}  // namespace

esp_err_t start_ota_updater() {
    const BaseType_t result =
        xTaskCreate(&ota_task, "ota_updater", TASK_STACK_SIZE, nullptr, TASK_PRIORITY, nullptr);
    return result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

}  // namespace reflowCtrl
