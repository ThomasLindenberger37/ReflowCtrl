#include "characterization_storage.hpp"

#include <memory>
#include <new>
#include <string_view>

#include "characterization_configuration.hpp"
#include "esp_log.h"
#include "nvs.h"

namespace reflowCtrl {
namespace {

constexpr char TAG[] = "characterization_store";
constexpr char NVS_NAMESPACE[] = "characterize";
constexpr char NVS_KEY[] = "config_v1";

esp_err_t read_configuration(std::unique_ptr<char[]>& data, std::size_t& size) {
    nvs_handle_t handle{};
    esp_err_t result = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (result == ESP_OK) {
        result = nvs_get_blob(handle, NVS_KEY, nullptr, &size);
        if (result == ESP_OK) {
            if (size == 0 || size > MAX_CHARACTERIZATION_CONFIGURATION_SIZE) {
                result = ESP_ERR_INVALID_SIZE;
            } else {
                data.reset(new (std::nothrow) char[size]);
                result = data ? nvs_get_blob(handle, NVS_KEY, data.get(), &size) : ESP_ERR_NO_MEM;
            }
        }
        nvs_close(handle);
    }
    return result;
}

esp_err_t json_error(httpd_req_t* request, const char* status, const char* body) {
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, body);
}

esp_err_t storage_error(httpd_req_t* request, esp_err_t error) {
    ESP_LOGE(TAG, "Configuration storage failed: %s", esp_err_to_name(error));
    return json_error(request, "500 Internal Server Error",
                      "{\"detail\":\"Could not access saved characterization\"}");
}

esp_err_t load_configuration(httpd_req_t* request) {
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    std::size_t size = 0;
    std::unique_ptr<char[]> data;
    const esp_err_t result = read_configuration(data, size);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        httpd_resp_set_type(request, "application/json");
        return httpd_resp_sendstr(request, "null");
    }
    if (result != ESP_OK) {
        return storage_error(request, result);
    }
    if (!is_valid_characterization_configuration({data.get(), size})) {
        return storage_error(request, ESP_ERR_INVALID_ARG);
    }
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, data.get(), static_cast<ssize_t>(size));
}

esp_err_t save_configuration(httpd_req_t* request) {
    if (request->content_len == 0
        || request->content_len > MAX_CHARACTERIZATION_CONFIGURATION_SIZE) {
        return json_error(request, "400 Bad Request",
                          "{\"detail\":\"Configuration must contain 1 to 8192 bytes\"}");
    }
    const std::unique_ptr<char[]> data(new (std::nothrow) char[request->content_len]);
    if (!data) {
        return storage_error(request, ESP_ERR_NO_MEM);
    }
    std::size_t received = 0;
    while (received < request->content_len) {
        const int length =
            httpd_req_recv(request, data.get() + received, request->content_len - received);
        if (length <= 0) {
            // Close the connection rather than reuse a partially consumed request.
            httpd_resp_send_err(request, HTTPD_408_REQ_TIMEOUT, "Incomplete configuration");
            return ESP_FAIL;
        }
        received += static_cast<std::size_t>(length);
    }
    if (!is_valid_characterization_configuration({data.get(), received})) {
        return json_error(
            request, "400 Bad Request",
            "{\"detail\":\"Invalid characterization configuration (version 1 required)\"}");
    }
    nvs_handle_t handle{};
    esp_err_t result = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (result == ESP_OK) {
        result = nvs_set_blob(handle, NVS_KEY, data.get(), received);
        if (result == ESP_OK) {
            result = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    if (result != ESP_OK) {
        return storage_error(request, result);
    }
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"saved\":true}");
}

}  // namespace

esp_err_t register_characterization_storage(httpd_handle_t server) {
    const httpd_uri_t load{"/api/characterization/configuration", HTTP_GET, &load_configuration,
                           nullptr};
    const httpd_uri_t save{"/api/characterization/configuration", HTTP_PUT, &save_configuration,
                           nullptr};
    const esp_err_t result = httpd_register_uri_handler(server, &load);
    return result == ESP_OK ? httpd_register_uri_handler(server, &save) : result;
}

esp_err_t load_saved_characterization(std::string& configuration) {
    std::size_t size = 0;
    std::unique_ptr<char[]> data;
    const esp_err_t result = read_configuration(data, size);
    if (result == ESP_OK) {
        configuration.assign(data.get(), size);
    }
    return result;
}

}  // namespace reflowCtrl
