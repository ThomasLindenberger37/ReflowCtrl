#pragma once

#include <cstdint>

#include "components/digital_io.hpp"
#include "esp_err.h"

namespace reflowCtrl {

class Max6675 {
   public:
    Max6675(DigitalInput& serial_output, DigitalOutput& clock, DigitalOutput& chip_select) noexcept
        : serial_output_(serial_output), clock_(clock), chip_select_(chip_select) {}

    esp_err_t initialize() noexcept;
    esp_err_t read_celsius(float& temperature_celsius, std::uint16_t& raw_frame) noexcept;

   private:
    [[nodiscard]] std::uint16_t read_frame() noexcept;

    DigitalInput& serial_output_;
    DigitalOutput& clock_;
    DigitalOutput& chip_select_;
};

}  // namespace reflowCtrl
