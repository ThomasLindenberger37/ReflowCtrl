#include "hardware/gpio_pins.hpp"

#include "esp_check.h"

namespace reflowCtrl {

esp_err_t GpioOutput::initialize(const bool initial_state) noexcept {
    ESP_RETURN_ON_ERROR(gpio_reset_pin(pin_), "gpio_pins", "Could not reset output pin");
    ESP_RETURN_ON_ERROR(gpio_set_direction(pin_, GPIO_MODE_OUTPUT), "gpio_pins",
                        "Could not configure output pin");
    return gpio_set_level(pin_, initial_state ? 1 : 0);
}

void GpioOutput::set(const bool is_on) noexcept {
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_set_level(pin_, is_on ? 1 : 0));
}

esp_err_t GpioInput::initialize(const gpio_pull_mode_t pull_mode) noexcept {
    ESP_RETURN_ON_ERROR(gpio_reset_pin(pin_), "gpio_pins", "Could not reset input pin");
    ESP_RETURN_ON_ERROR(gpio_set_direction(pin_, GPIO_MODE_INPUT), "gpio_pins",
                        "Could not configure input pin");
    return gpio_set_pull_mode(pin_, pull_mode);
}

bool GpioInput::is_active() const noexcept {
    const bool level_is_high = gpio_get_level(pin_) != 0;
    return active_low_ ? !level_is_high : level_is_high;
}

}  // namespace reflowCtrl
