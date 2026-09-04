#ifndef REFLOWCTRL_MESSAGES_HPP
#define REFLOWCTRL_MESSAGES_HPP

#include <array>
#include <cstdint>
#include <variant>

namespace reflowCtrl {

struct ButtonPressed {
    std::uint8_t button = 0;
};

struct TemperatureMeasured {
    float temperature_celsius = 0.0F;
};

struct TemperatureSensorFailed {};

struct OtaUpdateRequested {
    std::array<char, 16> server_address{};
};

struct OtaUpdateStarted {};

struct ControllerStarted {};

using MessageTypes = std::variant<ButtonPressed, TemperatureMeasured, TemperatureSensorFailed,
                                  OtaUpdateRequested, OtaUpdateStarted, ControllerStarted>;

}  // namespace reflowCtrl

#endif  // REFLOWCTRL_MESSAGES_HPP
