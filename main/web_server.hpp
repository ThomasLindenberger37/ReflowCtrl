#ifndef REFLOWCTRL_WEB_SERVER_HPP
#define REFLOWCTRL_WEB_SERVER_HPP

#include <atomic>

#include "components/characterization_controller.hpp"
#include "esp_err.h"
#include "esp_http_server.h"
#include "message_bus.hpp"

namespace reflowCtrl {

class WebServer {
   public:
    WebServer(MessageBus& bus, CharacterizationController& characterization) noexcept
        : bus_(bus), characterization_(characterization) {}

    esp_err_t start();
    void tick() noexcept;
    void publish_ota_request(const OtaUpdateRequested& request) noexcept {
        bus_.publish(request);
    }
    void start_characterization() noexcept;
    void abort_characterization() noexcept;
    [[nodiscard]] CharacterizationStatus characterization_status() noexcept {
        return characterization_.status();
    }
    [[nodiscard]] CharacterizationLiveFeed::PreviewSnapshot characterization_preview_after(
        std::uint32_t cursor) noexcept {
        return characterization_.preview_after(cursor);
    }

   private:
    static void acknowledge_health_probe(void* context) noexcept;
    esp_err_t start_services() noexcept;
    esp_err_t start_mdns() noexcept;
    esp_err_t start_http_server() noexcept;
    void restart_services() noexcept;

    MessageBus& bus_;
    CharacterizationController& characterization_;
    httpd_handle_t server_{nullptr};
    bool mdns_started_{false};
    std::atomic<std::uint32_t> health_probe_acknowledgements_{0};
    std::uint32_t pending_health_probe_{0};
    std::uint8_t failed_health_probes_{0};
    std::uint8_t failed_mdns_announcements_{0};
    std::int64_t next_health_check_us_{0};
    std::int64_t next_mdns_announcement_us_{0};
    std::int64_t restart_not_before_us_{0};
};

}  // namespace reflowCtrl

#endif  // REFLOWCTRL_WEB_SERVER_HPP
