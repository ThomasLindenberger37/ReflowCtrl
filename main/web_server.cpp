#include "web_server.hpp"

#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_log_write.h"
#include "esp_netif.h"
#include "ipv4_address.hpp"
#include "log_buffer.hpp"
#include "mdns.h"

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");
extern const char style_css_start[] asm("_binary_style_css_start");
extern const char style_css_end[] asm("_binary_style_css_end");
extern const char app_js_start[] asm("_binary_app_js_start");
extern const char app_js_end[] asm("_binary_app_js_end");
extern const char reflow_profile_js_start[] asm("_binary_reflow_profile_js_start");
extern const char reflow_profile_js_end[] asm("_binary_reflow_profile_js_end");

namespace reflowCtrl {
namespace {

constexpr char TAG[] = "web_server";
constexpr char HOSTNAME[] = "reflow-ctrl";
constexpr uint16_t HTTP_PORT = 80;
constexpr std::size_t SERVER_ADDRESS_SIZE = 16;
constexpr std::size_t LOG_FORMAT_BUFFER_SIZE = 256;
LogBuffer log_buffer;
vprintf_like_t uart_vprintf = nullptr;

struct WebAsset {
    const char* begin;
    const char* end;
    const char* content_type;
};

int capture_log(const char* format, va_list arguments) {
    va_list uart_arguments;
    va_copy(uart_arguments, arguments);
    const int result = uart_vprintf(format, uart_arguments);
    va_end(uart_arguments);

    char line[LOG_FORMAT_BUFFER_SIZE]{};
    va_list buffer_arguments;
    va_copy(buffer_arguments, arguments);
    const int length = std::vsnprintf(line, sizeof(line), format, buffer_arguments);
    va_end(buffer_arguments);
    if (length > 0) {
        std::size_t stored_length = std::strlen(line);
        while (stored_length > 0
               && (line[stored_length - 1] == '\n' || line[stored_length - 1] == '\r')) {
            --stored_length;
        }
        if (stored_length > 0) {
            log_buffer.append({line, stored_length});
        }
    }
    return result;
}

esp_err_t send_asset(httpd_req_t* request) {
    const auto* asset = static_cast<const WebAsset*>(request->user_ctx);
    httpd_resp_set_type(request, asset->content_type);
    httpd_resp_set_hdr(request, "Cache-Control", "no-store, max-age=0");
    httpd_resp_set_hdr(request, "Pragma", "no-cache");
    // EMBED_TXTFILES appends a NUL byte. Sending it makes JavaScript invalid.
    const std::ptrdiff_t length = asset->end - asset->begin - 1;
    return httpd_resp_send(request, asset->begin, length);
}

esp_err_t send_status(httpd_req_t* request) {
    const auto* server = static_cast<const WebServer*>(request->user_ctx);
    const bool has_temperature = server->has_temperature();
    const float temperature = server->temperature_celsius();
    char response[160]{};
    std::snprintf(response, sizeof(response),
                  "{\"state\":\"%s\",\"temperature\":%.2f,\"target_temperature\":0.0,"
                  "\"heater\":%s,\"elapsed_seconds\":0}",
                  has_temperature ? "idle" : "error", static_cast<double>(temperature),
                  server->heater_active() ? "true" : "false");
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, response);
}

void append_json_escaped(char*& output, std::size_t& remaining, const char* text) {
    while (*text != '\0' && remaining > 1) {
        const char character = *text++;
        if ((character == '"' || character == '\\') && remaining > 2) {
            *output++ = '\\';
            --remaining;
        }
        if (static_cast<unsigned char>(character) >= 0x20) {
            *output++ = character;
            --remaining;
        }
    }
    *output = '\0';
}

esp_err_t send_logs(httpd_req_t* request) {
    std::uint32_t cursor = 0;
    char query[48]{};
    char cursor_text[16]{};
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK
        && httpd_query_key_value(query, "after", cursor_text, sizeof(cursor_text)) == ESP_OK) {
        cursor = static_cast<std::uint32_t>(std::strtoul(cursor_text, nullptr, 10));
    }

    const LogBuffer::Snapshot snapshot = log_buffer.after(cursor);
    std::array<char, 2048> response{};
    char* output = response.data();
    std::size_t remaining = response.size();
    int written = std::snprintf(output, remaining, "{\"lines\":[");
    output += written;
    remaining -= static_cast<std::size_t>(written);
    for (std::size_t index = 0; index < snapshot.count; ++index) {
        written =
            std::snprintf(output, remaining, "%s{\"id\":%lu,\"text\":\"", index == 0 ? "" : ",",
                          static_cast<unsigned long>(snapshot.entries[index].id));
        output += written;
        remaining -= static_cast<std::size_t>(written);
        append_json_escaped(output, remaining, snapshot.entries[index].text.data());
        written = std::snprintf(output, remaining, "\"}");
        output += written;
        remaining -= static_cast<std::size_t>(written);
    }
    std::snprintf(output, remaining, "],\"next_cursor\":%lu}",
                  static_cast<unsigned long>(snapshot.next_cursor));
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, response.data());
}

esp_err_t announce_mdns(esp_netif_t* network_interface) {
    constexpr auto ACTIONS =
        static_cast<mdns_event_actions_t>(MDNS_EVENT_ENABLE_IP4 | MDNS_EVENT_ANNOUNCE_IP4);
    return mdns_netif_action(network_interface, ACTIONS);
}

void handle_ip_event(void*, esp_event_base_t, int32_t event_id, void* event_data) {
    if (event_id != IP_EVENT_STA_GOT_IP) {
        return;
    }

    auto* event = static_cast<ip_event_got_ip_t*>(event_data);
    const esp_err_t result = announce_mdns(event->esp_netif);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to re-announce mDNS after Wi-Fi reconnect: %s",
                 esp_err_to_name(result));
    } else {
        ESP_LOGI(TAG, "Re-announced %s.local", HOSTNAME);
    }
}

esp_err_t handle_ota_trigger(httpd_req_t* request) {
    if (request->content_len <= 0
        || request->content_len >= static_cast<int>(SERVER_ADDRESS_SIZE)) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Expected an IPv4 address");
    }

    char server_address[SERVER_ADDRESS_SIZE]{};
    const int received = httpd_req_recv(request, server_address, request->content_len);
    if (received != request->content_len || !is_valid_ipv4_address(server_address)) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid IPv4 address");
    }

    auto* server = static_cast<WebServer*>(request->user_ctx);
    OtaUpdateRequested update_request{};
    std::memcpy(update_request.server_address.data(), server_address,
                update_request.server_address.size());
    server->publish_ota_request(update_request);

    httpd_resp_set_status(request, "202 Accepted");
    httpd_resp_set_type(request, "text/plain");
    return httpd_resp_sendstr(request, "OTA update accepted\n");
}

esp_err_t handle_characterization_start(httpd_req_t* request) {
    auto* server = static_cast<WebServer*>(request->user_ctx);
    server->start_characterization();
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{}");
}

esp_err_t handle_characterization_abort(httpd_req_t* request) {
    auto* server = static_cast<WebServer*>(request->user_ctx);
    server->abort_characterization();
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{}");
}

}  // namespace

esp_err_t WebServer::start() {
    if (!bus_.subscribe<TemperatureMeasured>(&WebServer::on_temperature_measured, this)
        || !bus_.subscribe<TemperatureSensorFailed>(&WebServer::on_temperature_sensor_failed,
                                                    this)) {
        return ESP_ERR_NO_MEM;
    }
    uart_vprintf = esp_log_set_vprintf(&capture_log);
    ESP_RETURN_ON_ERROR(mdns_init(), TAG, "Failed to initialize mDNS");
    ESP_RETURN_ON_ERROR(mdns_hostname_set(HOSTNAME), TAG, "Failed to set mDNS hostname");
    ESP_RETURN_ON_ERROR(mdns_instance_name_set("ReflowCtrl"), TAG,
                        "Failed to set mDNS instance name");
    ESP_RETURN_ON_ERROR(mdns_service_add("ReflowCtrl", "_http", "_tcp", HTTP_PORT, nullptr, 0), TAG,
                        "Failed to advertise HTTP service");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &handle_ip_event, nullptr), TAG,
        "Failed to register mDNS reconnect handler");

    esp_netif_t* station_interface = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (station_interface == nullptr) {
        return ESP_ERR_NOT_FOUND;
    }
    ESP_RETURN_ON_ERROR(announce_mdns(station_interface), TAG,
                        "Failed to announce mDNS on station interface");

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = HTTP_PORT;
    config.stack_size = 6144;
    config.max_uri_handlers = 10;
    httpd_handle_t server = nullptr;
    ESP_RETURN_ON_ERROR(httpd_start(&server, &config), TAG, "Failed to start HTTP server");

    static WebAsset index_asset{index_html_start, index_html_end, "text/html"};
    static WebAsset style_asset{style_css_start, style_css_end, "text/css"};
    static WebAsset app_asset{app_js_start, app_js_end, "application/javascript"};
    static WebAsset profile_asset{reflow_profile_js_start, reflow_profile_js_end,
                                  "application/javascript"};
    const std::array endpoints{
        httpd_uri_t{"/", HTTP_GET, &send_asset, &index_asset},
        httpd_uri_t{"/index.html", HTTP_GET, &send_asset, &index_asset},
        httpd_uri_t{"/style.css", HTTP_GET, &send_asset, &style_asset},
        httpd_uri_t{"/app.js", HTTP_GET, &send_asset, &app_asset},
        httpd_uri_t{"/reflow-profile.js", HTTP_GET, &send_asset, &profile_asset},
        httpd_uri_t{"/api/status", HTTP_GET, &send_status, this},
        httpd_uri_t{"/api/logs", HTTP_GET, &send_logs, nullptr},
        httpd_uri_t{"/ota", HTTP_POST, &handle_ota_trigger, this},
        httpd_uri_t{"/api/characterization/start", HTTP_POST, &handle_characterization_start, this},
        httpd_uri_t{"/api/characterization/abort", HTTP_POST, &handle_characterization_abort, this},
    };

    for (const httpd_uri_t& endpoint : endpoints) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &endpoint), TAG,
                            "Failed to register %s", endpoint.uri);
    }

    ESP_LOGI(TAG, "Listening at http://%s.local", HOSTNAME);
    return ESP_OK;
}

void WebServer::on_temperature_measured(const TemperatureMeasured& message) noexcept {
    temperature_celsius_.store(message.temperature_celsius);
    has_temperature_.store(true);
}

void WebServer::on_temperature_sensor_failed(const TemperatureSensorFailed&) noexcept {
    has_temperature_.store(false);
}

void WebServer::start_characterization() noexcept {
    heater_active_.store(true);
    bus_.publish(CharacterizationStarted{});
}

void WebServer::abort_characterization() noexcept {
    heater_active_.store(false);
    bus_.publish(CharacterizationAborted{});
}

}  // namespace reflowCtrl
