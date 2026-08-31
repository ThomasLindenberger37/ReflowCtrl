#pragma once

#include <cstdint>

#include "driver/gpio.h"
#include "esp_err.h"

namespace reflowCtrl {

class Max6675 {
   public:
    static constexpr gpio_num_t SO_MISO_GPIO = GPIO_NUM_25;
    static constexpr gpio_num_t SCK_GPIO = GPIO_NUM_32;
    static constexpr gpio_num_t CS_GPIO = GPIO_NUM_27;

    esp_err_t initialize() noexcept;
    esp_err_t read_celsius(float& temperature_celsius, std::uint16_t& raw_frame) noexcept;

   private:
    [[nodiscard]] std::uint16_t read_frame() noexcept;
};

}  // namespace reflowCtrl
