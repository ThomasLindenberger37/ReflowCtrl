#ifndef REFLOWCTRL_WEB_SERVER_HPP
#define REFLOWCTRL_WEB_SERVER_HPP

#include "components/characterization_controller.hpp"
#include "components/reflow_controller.hpp"
#include "esp_err.h"
#include "esp_http_server.h"
#include "message_bus.hpp"

namespace reflowCtrl {

class WebServer {
   public:
    WebServer(MessageBus& bus, CharacterizationController& characterization,
              ReflowController& reflow) noexcept
        : bus_(bus), characterization_(characterization), reflow_(reflow) {}

    esp_err_t start();
    void publish_ota_request(const OtaUpdateRequested& request) noexcept {
        bus_.publish(request);
    }
    void start_characterization() noexcept;
    void abort_characterization() noexcept;
    [[nodiscard]] bool start_reflow() noexcept;
    void abort_reflow() noexcept;
    [[nodiscard]] ControllerTelemetry controller_telemetry() noexcept {
        return reflow_.telemetry();
    }
    [[nodiscard]] CharacterizationStatus characterization_status() noexcept {
        return characterization_.status();
    }
    [[nodiscard]] CharacterizationLiveFeed::PreviewSnapshot characterization_preview_after(
        std::uint32_t cursor) noexcept {
        return characterization_.preview_after(cursor);
    }

   private:
    esp_err_t start_http_server() noexcept;

    MessageBus& bus_;
    CharacterizationController& characterization_;
    ReflowController& reflow_;
    httpd_handle_t server_{nullptr};
};

}  // namespace reflowCtrl

#endif  // REFLOWCTRL_WEB_SERVER_HPP
