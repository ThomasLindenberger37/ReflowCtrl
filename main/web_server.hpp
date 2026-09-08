#ifndef REFLOWCTRL_WEB_SERVER_HPP
#define REFLOWCTRL_WEB_SERVER_HPP

#include "components/characterization_controller.hpp"
#include "esp_err.h"
#include "message_bus.hpp"

namespace reflowCtrl {

class WebServer {
   public:
    WebServer(MessageBus& bus, CharacterizationController& characterization) noexcept
        : bus_(bus), characterization_(characterization) {}

    esp_err_t start();
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
    MessageBus& bus_;
    CharacterizationController& characterization_;
};

}  // namespace reflowCtrl

#endif  // REFLOWCTRL_WEB_SERVER_HPP
