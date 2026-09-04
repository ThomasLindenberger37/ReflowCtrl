#include "components/status_led.hpp"

namespace reflowCtrl {

esp_err_t StatusLed::start() {
    return bus_.subscribe<OtaUpdateStarted>(&StatusLed::on_ota_started, this)
                   && bus_.subscribe<WifiConnecting>(&StatusLed::on_wifi_connecting, this)
                   && bus_.subscribe<WifiConnected>(&StatusLed::on_wifi_connected, this)
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}

void StatusLed::tick(const std::uint32_t elapsed_ms) noexcept {
    output_.set(blinker_.is_on());
    blinker_.advance(elapsed_ms);
}

void StatusLed::on_ota_started(const OtaUpdateStarted&) noexcept {
    ota_update_in_progress_ = true;
    blinker_.request_mode(LedBlinkMode::TenHertz);
}

void StatusLed::on_wifi_connecting(const WifiConnecting&) noexcept {
    if (!ota_update_in_progress_) {
        blinker_.request_mode(LedBlinkMode::Slow);
    }
}

void StatusLed::on_wifi_connected(const WifiConnected&) noexcept {
    if (!ota_update_in_progress_) {
        blinker_.request_mode(LedBlinkMode::On);
    }
}

}  // namespace reflowCtrl
