#include "components/temperature_controller.hpp"

#include <algorithm>
#include <cmath>

namespace reflowCtrl {

HeaterPower TemperatureController::update(const TemperatureControlInput& input) noexcept {
    if (input.target_ramp_c_per_s < 0.0F) {
        power_ = HeaterPower::Off;
        return power_;
    }

    const float error = input.target_temperature_c - input.actual_temperature_c;
    if (error < -config_.hysteresis_c) {
        power_ = HeaterPower::Off;
        return power_;
    }
    const float projected_error =
        input.target_temperature_c + input.target_ramp_c_per_s * config_.thermal_lookahead_s
        - (input.actual_temperature_c
           + std::max(0.0F, input.actual_ramp_c_per_s) * config_.thermal_lookahead_s);
    if (projected_error < -config_.hysteresis_c) {
        power_ = HeaterPower::Off;
        return power_;
    }
    if (std::abs(error) <= config_.hysteresis_c) {
        return power_;
    }

    int steps = 0;
    if (input.characterized_heating_rate_c_per_s > 0.0F) {
        const float feedforward = std::clamp(
            input.target_ramp_c_per_s / input.characterized_heating_rate_c_per_s, 0.0F, 1.0F);
        steps = static_cast<int>(std::lround(feedforward * 4.0F));
    }

    const int correction = std::max(
        1, static_cast<int>(std::ceil(std::abs(error) / config_.error_per_feedback_step_c)));
    steps += error > 0.0F ? correction : -correction;
    if (std::abs(input.target_ramp_c_per_s) <= config_.steady_target_ramp_c_per_s
        && error < config_.large_steady_error_c) {
        steps = std::min(steps, 2);
    }
    power_ = from_steps(steps);
    return power_;
}

HeaterPower TemperatureController::from_steps(const int steps) noexcept {
    switch (std::clamp(steps, 0, 4)) {
        case 1:
            return HeaterPower::P25;
        case 2:
            return HeaterPower::P50;
        case 3:
            return HeaterPower::P75;
        case 4:
            return HeaterPower::P100;
        default:
            return HeaterPower::Off;
    }
}

}  // namespace reflowCtrl
