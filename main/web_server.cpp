#include "web_server.hpp"

#include <cstddef>

#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "lwip/inet.h"
#include "mdns.h"
#include "ota_updater.hpp"

namespace reflowCtrl {
namespace {

constexpr char TAG[] = "web_server";
constexpr char HOSTNAME[] = "reflow-ctrl";
constexpr uint16_t HTTP_PORT = 80;
constexpr std::size_t SERVER_ADDRESS_SIZE = 16;

esp_err_t handle_status(httpd_req_t* request) {
    httpd_resp_set_type(request, "text/plain");
    return httpd_resp_sendstr(request, "ReflowCtrl ready\n");
}

esp_err_t handle_ota_trigger(httpd_req_t* request) {
    if (request->content_len <= 0
        || request->content_len >= static_cast<int>(SERVER_ADDRESS_SIZE)) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Expected an IPv4 address");
    }

    char server_address[SERVER_ADDRESS_SIZE]{};
    const int received = httpd_req_recv(request, server_address, request->content_len);
    in_addr parsed_address{};
    if (received != request->content_len
        || inet_pton(AF_INET, server_address, &parsed_address) != 1) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid IPv4 address");
    }

    const esp_err_t result = trigger_ota_update(server_address);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Could not trigger OTA update: %s", esp_err_to_name(result));
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "OTA updater unavailable");
    }

    httpd_resp_set_status(request, "202 Accepted");
    httpd_resp_set_type(request, "text/plain");
    return httpd_resp_sendstr(request, "OTA update accepted\n");
}

}  // namespace

esp_err_t start_web_server() {
    ESP_RETURN_ON_ERROR(mdns_init(), TAG, "Failed to initialize mDNS");
    ESP_RETURN_ON_ERROR(mdns_hostname_set(HOSTNAME), TAG, "Failed to set mDNS hostname");
    ESP_RETURN_ON_ERROR(mdns_instance_name_set("ReflowCtrl"), TAG,
                        "Failed to set mDNS instance name");
    ESP_RETURN_ON_ERROR(mdns_service_add("ReflowCtrl", "_http", "_tcp", HTTP_PORT, nullptr, 0), TAG,
                        "Failed to advertise HTTP service");

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = HTTP_PORT;
    httpd_handle_t server = nullptr;
    ESP_RETURN_ON_ERROR(httpd_start(&server, &config), TAG, "Failed to start HTTP server");

    const httpd_uri_t status_endpoint{
        .uri = "/",
        .method = HTTP_GET,
        .handler = handle_status,
        .user_ctx = nullptr,
    };
    const httpd_uri_t ota_endpoint{
        .uri = "/ota",
        .method = HTTP_POST,
        .handler = handle_ota_trigger,
        .user_ctx = nullptr,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &status_endpoint), TAG,
                        "Failed to register status endpoint");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &ota_endpoint), TAG,
                        "Failed to register OTA endpoint");

    ESP_LOGI(TAG, "Listening at http://%s.local", HOSTNAME);
    return ESP_OK;
}

}  // namespace reflowCtrl
