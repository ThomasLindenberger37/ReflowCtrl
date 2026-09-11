#ifndef REFLOWCTRL_TEMPERATURE_CONTROLLER_HPP
#define REFLOWCTRL_TEMPERATURE_CONTROLLER_HPP

#include <cstdint>

namespace reflowCtrl {

constexpr float DEFAULT_HYSTERESIS_C = 2.0F;

enum class HeaterPower : std::uint8_t { Off = 0, P25 = 25, P50 = 50, P75 = 75, P100 = 100 };

struct TemperatureControllerConfig {
    float hysteresis_c = DEFAULT_HYSTERESIS_C;
    float error_per_feedback_step_c = 5.0F;
    float steady_target_ramp_c_per_s = 0.1F;
    float large_steady_error_c = 20.0F;
    float thermal_lookahead_s = 6.0F;
};

struct TemperatureControlInput {
    float target_temperature_c = 0.0F;
    float actual_temperature_c = 0.0F;
    float target_ramp_c_per_s = 0.0F;
    float actual_ramp_c_per_s = 0.0F;
    float characterized_heating_rate_c_per_s = 0.0F;
};

class TemperatureController {
   public:
    explicit TemperatureController(TemperatureControllerConfig config = {}) noexcept
        : config_(config) {}

    [[nodiscard]] HeaterPower update(const TemperatureControlInput& input) noexcept;
    void reset() noexcept {
        power_ = HeaterPower::Off;
    }

   private:
    [[nodiscard]] static HeaterPower from_steps(int steps) noexcept;

    TemperatureControllerConfig config_;
    HeaterPower power_ = HeaterPower::Off;
};

[[nodiscard]] constexpr std::uint8_t heater_power_percent(const HeaterPower power) noexcept {
    return static_cast<std::uint8_t>(power);
}

}  // namespace reflowCtrl

#endif  // REFLOWCTRL_TEMPERATURE_CONTROLLER_HPP
