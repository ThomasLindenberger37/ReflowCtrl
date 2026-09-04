#ifndef REFLOWCTRL_RELAY_OUTPUT_HPP
#define REFLOWCTRL_RELAY_OUTPUT_HPP

#include "components/digital_io.hpp"
#include "esp_err.h"
#include "message_bus.hpp"

namespace reflowCtrl {

class RelayOutput {
   public:
    RelayOutput(MessageBus& bus, DigitalOutput& output) noexcept : bus_(bus), output_(output) {}

    esp_err_t start();
    void on_ota_started(const OtaUpdateStarted&) noexcept;
    void on_heater_output_requested(const HeaterOutputRequested& request) noexcept;

   private:
    MessageBus& bus_;
    DigitalOutput& output_;
};

}  // namespace reflowCtrl

#endif  // REFLOWCTRL_RELAY_OUTPUT_HPP
