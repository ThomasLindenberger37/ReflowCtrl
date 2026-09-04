#include "hardware/hardware_configuration.hpp"

namespace reflowCtrl {
namespace {

constexpr HardwareConfiguration CONFIGURATION{
    .status_led_pin = GPIO_NUM_23,
    .relay_pin = GPIO_NUM_16,
    .button_pin = GPIO_NUM_0,
    .max6675_so_pin = GPIO_NUM_25,
    .max6675_sck_pin = GPIO_NUM_32,
    .max6675_cs_pin = GPIO_NUM_27,
};

}  // namespace

const HardwareConfiguration& hardware_configuration() noexcept {
    return CONFIGURATION;
}

}  // namespace reflowCtrl
