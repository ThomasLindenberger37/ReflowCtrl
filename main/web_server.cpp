#include "web_server.hpp"

#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "characterization_storage.hpp"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_log_write.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "ipv4_address.hpp"
#include "log_buffer.hpp"
#include "mdns.h"
#include "profile_storage.hpp"

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");
extern const char style_css_start[] asm("_binary_style_css_start");
extern const char style_css_end[] asm("_binary_style_css_end");
extern const char chart_umd_min_js_start[] asm("_binary_chart_umd_min_js_start");
extern const char chart_umd_min_js_end[] asm("_binary_chart_umd_min_js_end");
extern const char app_js_start[] asm("_binary_app_js_start");
extern const char app_js_end[] asm("_binary_app_js_end");
extern const char characterization_analyzer_js_start[] asm(
    "_binary_characterization_analyzer_js_start");
extern const char characterization_analyzer_js_end[] asm(
    "_binary_characterization_analyzer_js_end");

namespace reflowCtrl {
namespace {

constexpr char TAG[] = "web_server";
constexpr char HOSTNAME[] = "reflow-ctrl";
constexpr uint16_t HTTP_PORT = 80;
constexpr std::size_t SERVER_ADDRESS_SIZE = 16;
constexpr std::size_t LOG_FORMAT_BUFFER_SIZE = 256;
constexpr std::int64_t HEALTH_CHECK_INTERVAL_US = 5'000'000;
constexpr std::int64_t MDNS_ANNOUNCEMENT_INTERVAL_US = 30'000'000;
constexpr std::int64_t RESTART_COOLDOWN_US = 60'000'000;
constexpr std::uint8_t FAILED_HEALTH_PROBES_BEFORE_RESTART = 6;
constexpr std::uint8_t FAILED_MDNS_ANNOUNCEMENTS_BEFORE_RESTART = 3;
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
    auto* server = static_cast<WebServer*>(request->user_ctx);
    const CharacterizationStatus characterization = server->characterization_status();
    char response[160]{};
    std::snprintf(response, sizeof(response),
                  "{\"state\":\"%s\",\"temperature\":%.2f,\"target_temperature\":0.0,"
                  "\"heater\":%s,\"elapsed_seconds\":0}",
                  characterization.has_temperature ? "idle" : "error",
                  static_cast<double>(characterization.temperature_celsius),
                  characterization.heater_output ? "true" : "false");
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
        // The mDNS API explicitly accepts combined event flags.
        // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
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

esp_err_t send_characterization_status(httpd_req_t* request) {
    auto* server = static_cast<WebServer*>(request->user_ctx);
    const CharacterizationStatus status = server->characterization_status();
    char response[256]{};
    std::snprintf(
        response, sizeof(response),
        "{\"running\":%s,\"phase\":\"%s\",\"elapsed_ms\":%lu,"
        "\"temperature\":%.2f,\"heater_output\":%s,\"error\":\"%s\"}",
        (status.phase >= CharacterizationPhase::baseline
         && status.phase <= CharacterizationPhase::cooldown)
            ? "true"
            : "false",
        characterization_phase_name(status.phase), static_cast<unsigned long>(status.elapsed_ms),
        static_cast<double>(status.temperature_celsius), status.heater_output ? "true" : "false",
        characterization_stop_reason_name(status.stop_reason));
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, response);
}

esp_err_t send_characterization_preview(httpd_req_t* request) {
    std::uint32_t cursor = 0;
    char query[48]{};
    char cursor_text[16]{};
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK
        && httpd_query_key_value(query, "after", cursor_text, sizeof(cursor_text)) == ESP_OK) {
        cursor = static_cast<std::uint32_t>(std::strtoul(cursor_text, nullptr, 10));
    }

    auto* server = static_cast<WebServer*>(request->user_ctx);
    const CharacterizationLiveFeed::PreviewSnapshot snapshot =
        server->characterization_preview_after(cursor);
    httpd_resp_set_type(request, "application/json");
    ESP_RETURN_ON_ERROR(httpd_resp_send_chunk(request, "{\"lines\":[", HTTPD_RESP_USE_STRLEN), TAG,
                        "Failed to send characterization preview");
    for (std::size_t index = 0; index < snapshot.count; ++index) {
        std::array<char, CharacterizationLiveFeed::LINE_SIZE + 4> line{};
        const int written = std::snprintf(line.data(), line.size(), "%s\"%s\"",
                                          index == 0 ? "" : ",", snapshot.lines[index].data());
        if (written < 0 || httpd_resp_send_chunk(request, line.data(), written) != ESP_OK) {
            return ESP_FAIL;
        }
    }
    std::array<char, 48> footer{};
    std::snprintf(footer.data(), footer.size(), "],\"next_cursor\":%lu}",
                  static_cast<unsigned long>(snapshot.next_cursor));
    ESP_RETURN_ON_ERROR(httpd_resp_send_chunk(request, footer.data(), HTTPD_RESP_USE_STRLEN), TAG,
                        "Failed to finish characterization preview");
    return httpd_resp_send_chunk(request, nullptr, 0);
}

}  // namespace

esp_err_t WebServer::start() {
    uart_vprintf = esp_log_set_vprintf(&capture_log);
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &handle_ip_event, nullptr), TAG,
        "Failed to register mDNS reconnect handler");
    ESP_RETURN_ON_ERROR(start_services(), TAG, "Failed to start network services");

    const std::int64_t now = esp_timer_get_time();
    next_health_check_us_ = now + HEALTH_CHECK_INTERVAL_US;
    next_mdns_announcement_us_ = now + MDNS_ANNOUNCEMENT_INTERVAL_US;
    return ESP_OK;
}

esp_err_t WebServer::start_mdns() noexcept {
    const esp_err_t init_result = mdns_init();
    if (init_result != ESP_OK) {
        return init_result;
    }
    mdns_started_ = true;

    esp_err_t result = mdns_hostname_set(HOSTNAME);
    if (result == ESP_OK) {
        result = mdns_instance_name_set("ReflowCtrl");
    }
    if (result == ESP_OK) {
        result = mdns_service_add("ReflowCtrl", "_http", "_tcp", HTTP_PORT, nullptr, 0);
    }
    esp_netif_t* station_interface = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (result == ESP_OK && station_interface == nullptr) {
        result = ESP_ERR_NOT_FOUND;
    }
    if (result == ESP_OK) {
        result = announce_mdns(station_interface);
    }
    if (result != ESP_OK) {
        mdns_free();
        mdns_started_ = false;
    }
    return result;
}

esp_err_t WebServer::start_http_server() noexcept {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = HTTP_PORT;
    config.stack_size = 8192;
    config.max_uri_handlers = 21;
    config.uri_match_fn = httpd_uri_match_wildcard;
    esp_err_t result = httpd_start(&server_, &config);
    if (result != ESP_OK) {
        server_ = nullptr;
        return result;
    }

    static WebAsset index_asset{index_html_start, index_html_end, "text/html"};
    static WebAsset style_asset{style_css_start, style_css_end, "text/css"};
    static WebAsset chart_asset{chart_umd_min_js_start, chart_umd_min_js_end,
                                "application/javascript"};
    static WebAsset app_asset{app_js_start, app_js_end, "application/javascript"};
    static WebAsset analyzer_asset{characterization_analyzer_js_start,
                                   characterization_analyzer_js_end, "application/javascript"};
    const std::array endpoints{
        httpd_uri_t{"/", HTTP_GET, &send_asset, &index_asset},
        httpd_uri_t{"/index.html", HTTP_GET, &send_asset, &index_asset},
        httpd_uri_t{"/style.css", HTTP_GET, &send_asset, &style_asset},
        httpd_uri_t{"/chart.umd.min.js", HTTP_GET, &send_asset, &chart_asset},
        httpd_uri_t{"/app.js", HTTP_GET, &send_asset, &app_asset},
        httpd_uri_t{"/characterization-analyzer.js", HTTP_GET, &send_asset, &analyzer_asset},
        httpd_uri_t{"/api/status", HTTP_GET, &send_status, this},
        httpd_uri_t{"/api/logs", HTTP_GET, &send_logs, nullptr},
        httpd_uri_t{"/ota", HTTP_POST, &handle_ota_trigger, this},
        httpd_uri_t{"/api/characterization/start", HTTP_POST, &handle_characterization_start, this},
        httpd_uri_t{"/api/characterization/stop", HTTP_POST, &handle_characterization_abort, this},
        httpd_uri_t{"/api/characterization/status", HTTP_GET, &send_characterization_status, this},
        httpd_uri_t{"/api/characterization/samples", HTTP_GET, &send_characterization_preview,
                    this},
    };

    for (const httpd_uri_t& endpoint : endpoints) {
        result = httpd_register_uri_handler(server_, &endpoint);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register %s: %s", endpoint.uri, esp_err_to_name(result));
            break;
        }
    }
    if (result == ESP_OK) {
        result = register_characterization_storage(server_);
    }
    if (result == ESP_OK) {
        result = register_profile_storage(server_);
    }
    if (result != ESP_OK) {
        httpd_stop(server_);
        server_ = nullptr;
        return result;
    }

    ESP_LOGI(TAG, "Listening at http://%s.local", HOSTNAME);
    return ESP_OK;
}

esp_err_t WebServer::start_services() noexcept {
    ESP_RETURN_ON_ERROR(start_mdns(), TAG, "Failed to initialize mDNS");
    const esp_err_t result = start_http_server();
    if (result != ESP_OK) {
        mdns_free();
        mdns_started_ = false;
    }
    return result;
}

void WebServer::acknowledge_health_probe(void* context) noexcept {
    auto* web_server = static_cast<WebServer*>(context);
    web_server->health_probe_acknowledgements_.fetch_add(1, std::memory_order_relaxed);
}

void WebServer::restart_services() noexcept {
    const std::int64_t now = esp_timer_get_time();
    if (now < restart_not_before_us_) {
        return;
    }
    restart_not_before_us_ = now + RESTART_COOLDOWN_US;
    ESP_LOGE(TAG, "HTTP or mDNS became unresponsive; restarting both services");
    if (server_ != nullptr) {
        const esp_err_t stop_result = httpd_stop(server_);
        if (stop_result != ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop HTTP server cleanly: %s", esp_err_to_name(stop_result));
        }
        server_ = nullptr;
    }
    if (mdns_started_) {
        mdns_free();
        mdns_started_ = false;
    }

    const esp_err_t result = start_services();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Network service restart failed: %s; retrying later",
                 esp_err_to_name(result));
    } else {
        ESP_LOGI(TAG, "HTTP and mDNS services restarted");
    }
    pending_health_probe_ = 0;
    failed_health_probes_ = 0;
    failed_mdns_announcements_ = 0;
}

void WebServer::tick() noexcept {
    const std::int64_t now = esp_timer_get_time();
    if (now >= next_mdns_announcement_us_) {
        next_mdns_announcement_us_ = now + MDNS_ANNOUNCEMENT_INTERVAL_US;
        esp_netif_t* station_interface = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (!mdns_started_ || station_interface == nullptr
            || announce_mdns(station_interface) != ESP_OK) {
            ++failed_mdns_announcements_;
            if (failed_mdns_announcements_ >= FAILED_MDNS_ANNOUNCEMENTS_BEFORE_RESTART) {
                restart_services();
                return;
            }
        } else {
            failed_mdns_announcements_ = 0;
        }
    }
    if (now < next_health_check_us_) {
        return;
    }
    next_health_check_us_ = now + HEALTH_CHECK_INTERVAL_US;

    bool health_probe_pending = false;
    if (pending_health_probe_ != 0) {
        health_probe_pending =
            health_probe_acknowledgements_.load(std::memory_order_relaxed) < pending_health_probe_;
        if (health_probe_pending) {
            ++failed_health_probes_;
        } else {
            failed_health_probes_ = 0;
        }
    }
    if (failed_health_probes_ >= FAILED_HEALTH_PROBES_BEFORE_RESTART) {
        restart_services();
        return;
    }
    if (server_ == nullptr) {
        restart_services();
        return;
    }
    if (health_probe_pending) {
        return;
    }

    pending_health_probe_ = health_probe_acknowledgements_.load(std::memory_order_relaxed) + 1;
    if (httpd_queue_work(server_, &WebServer::acknowledge_health_probe, this) != ESP_OK) {
        ++failed_health_probes_;
        pending_health_probe_ = 0;
        if (failed_health_probes_ >= FAILED_HEALTH_PROBES_BEFORE_RESTART) {
            restart_services();
        }
    }
}

void WebServer::start_characterization() noexcept {
    bus_.publish(CharacterizationStarted{});
}

void WebServer::abort_characterization() noexcept {
    bus_.publish(CharacterizationAborted{});
}

}  // namespace reflowCtrl
