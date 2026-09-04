#ifndef REFLOWCTRL_GPIO_PINS_HPP
#define REFLOWCTRL_GPIO_PINS_HPP

#include "components/digital_io.hpp"
#include "driver/gpio.h"
#include "esp_err.h"

namespace reflowCtrl {

class GpioOutput final : public DigitalOutput {
   public:
    explicit GpioOutput(gpio_num_t pin) noexcept : pin_(pin) {}

    esp_err_t initialize(bool initial_state) noexcept;
    void set(bool is_on) noexcept override;

   private:
    gpio_num_t pin_;
};

class GpioInput final : public DigitalInput {
   public:
    explicit GpioInput(gpio_num_t pin, bool active_low) noexcept
        : pin_(pin), active_low_(active_low) {}

    esp_err_t initialize(gpio_pull_mode_t pull_mode) noexcept;
    [[nodiscard]] bool is_active() const noexcept override;

   private:
    gpio_num_t pin_;
    bool active_low_;
};

}  // namespace reflowCtrl

#endif  // REFLOWCTRL_GPIO_PINS_HPP
