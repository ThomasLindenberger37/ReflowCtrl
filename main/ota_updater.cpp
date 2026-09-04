#include "ota_updater.hpp"

#include <array>
#include <cstdio>

#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "wifi_station.hpp"

namespace reflowCtrl {
namespace {

constexpr char TAG[] = "ota_updater";
constexpr TickType_t RETRY_PERIOD = pdMS_TO_TICKS(1000);
constexpr TickType_t CONFIRM_RETRY_PERIOD = pdMS_TO_TICKS(1000);
constexpr uint32_t TASK_STACK_SIZE = 8192;
constexpr UBaseType_t TASK_PRIORITY = configMAX_PRIORITIES - 1;
constexpr std::size_t SERVER_ADDRESS_SIZE = 16;
constexpr int OTA_RECEIVE_BUFFER_SIZE = 16 * 1024;

struct OtaRequest {
    std::array<char, SERVER_ADDRESS_SIZE> server_address{};
};

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

bool ota_update_with_host(const char* host) {
    const auto firmware_url = make_url(host, "/firmware.bin");

    esp_http_client_config_t http_config{};
    http_config.url = firmware_url.data();
    http_config.timeout_ms = 5000;
    http_config.keep_alive_enable = true;
    http_config.buffer_size = OTA_RECEIVE_BUFFER_SIZE;

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

}  // namespace

void OtaUpdater::task_entry(void* context) {
    static_cast<OtaUpdater*>(context)->task_loop();
}

void OtaUpdater::task_loop() {
    OtaRequest request{};
    while (true) {
        xQueueReceive(static_cast<QueueHandle_t>(request_queue_), &request, portMAX_DELAY);
        ESP_LOGI(TAG, "OTA update requested from %s", request.server_address.data());

        while (!is_wifi_connected() || !ota_update_with_host(request.server_address.data())) {
            vTaskDelay(RETRY_PERIOD);
        }

        ESP_LOGI(TAG, "Firmware image installed; confirming download to server");
        while (!confirm_update_with_host(request.server_address.data())) {
            ESP_LOGW(TAG, "Could not confirm update; retrying");
            vTaskDelay(CONFIRM_RETRY_PERIOD);
        }

        ESP_LOGI(TAG, "Update confirmed; restarting");
        esp_restart();
    }
}

esp_err_t OtaUpdater::start() {
    request_queue_ = xQueueCreate(1, sizeof(OtaRequest));
    if (request_queue_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    const BaseType_t result =
        xTaskCreate(&task_entry, "ota_updater", TASK_STACK_SIZE, this, TASK_PRIORITY, nullptr);
    if (result != pdPASS) {
        vQueueDelete(static_cast<QueueHandle_t>(request_queue_));
        request_queue_ = nullptr;
        return ESP_ERR_NO_MEM;
    }

    if (!bus_.subscribe<OtaUpdateRequested>(&OtaUpdater::on_update_requested, this)) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void OtaUpdater::on_update_requested(const OtaUpdateRequested& request) {
    OtaRequest queued_request{};
    queued_request.server_address = request.server_address;
    xQueueOverwrite(static_cast<QueueHandle_t>(request_queue_), &queued_request);
    bus_.publish(OtaUpdateStarted{});
}

}  // namespace reflowCtrl
