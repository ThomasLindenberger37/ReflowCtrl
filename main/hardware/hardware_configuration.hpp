#ifndef REFLOWCTRL_HARDWARE_CONFIGURATION_HPP
#define REFLOWCTRL_HARDWARE_CONFIGURATION_HPP

#include "driver/gpio.h"

namespace reflowCtrl {

struct HardwareConfiguration {
    gpio_num_t status_led_pin;
    gpio_num_t relay_pin;
    gpio_num_t button_pin;
    gpio_num_t max6675_so_pin;
    gpio_num_t max6675_sck_pin;
    gpio_num_t max6675_cs_pin;
};

[[nodiscard]] const HardwareConfiguration& hardware_configuration() noexcept;

}  // namespace reflowCtrl

#endif  // REFLOWCTRL_HARDWARE_CONFIGURATION_HPP
