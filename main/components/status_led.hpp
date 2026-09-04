#ifndef REFLOWCTRL_STATUS_LED_HPP
#define REFLOWCTRL_STATUS_LED_HPP

#include "components/digital_io.hpp"
#include "components/led_blinker.hpp"
#include "esp_err.h"
#include "message_bus.hpp"

namespace reflowCtrl {

class StatusLed {
   public:
    StatusLed(MessageBus& bus, DigitalOutput& output) noexcept : bus_(bus), output_(output) {}

    esp_err_t start();
    void tick(std::uint32_t elapsed_ms) noexcept;
    void on_ota_started(const OtaUpdateStarted&) noexcept;
    void on_wifi_connecting(const WifiConnecting&) noexcept;
    void on_wifi_connected(const WifiConnected&) noexcept;

   private:
    MessageBus& bus_;
    DigitalOutput& output_;
    LedBlinker blinker_{LedBlinkMode::Slow};
    bool ota_update_in_progress_ = false;
};

}  // namespace reflowCtrl

#endif  // REFLOWCTRL_STATUS_LED_HPP
