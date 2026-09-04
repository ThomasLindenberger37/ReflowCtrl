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

struct WifiConnecting {};

struct WifiConnected {};

enum class CharacterizationPhase : std::uint8_t {
    idle,
    baseline,
    heat_1,
    coast_1,
    heat_2,
    coast_2,
    heat_3,
    coast_3,
    heat_4,
    coast_4,
    final_heat,
    cooldown,
    completed,
    aborted,
    error,
};

struct CharacterizationStarted {};

struct CharacterizationAborted {};

struct HeaterOutputRequested {
    bool enabled = false;
};

struct OtaUpdateRequested {
    std::array<char, 16> server_address{};
};

struct OtaUpdateStarted {};

struct ControllerStarted {};

using MessageTypes =
    std::variant<ButtonPressed, TemperatureMeasured, TemperatureSensorFailed, WifiConnecting,
                 WifiConnected, CharacterizationStarted, CharacterizationAborted,
                 HeaterOutputRequested, OtaUpdateRequested, OtaUpdateStarted, ControllerStarted>;

}  // namespace reflowCtrl

#endif  // REFLOWCTRL_MESSAGES_HPP
