#include "components/safety_controller.hpp"

#include <cmath>

namespace reflowCtrl {

SafetyFault SafetyController::check_sample(const float temperature_c, const std::uint32_t now_ms,
                                           const bool has_previous_sample,
                                           const float previous_temperature_c,
                                           const std::uint32_t previous_sample_ms) const noexcept {
    if (!std::isfinite(temperature_c)) {
        return SafetyFault::InvalidTemperature;
    }
    if (temperature_c >= config_.maximum_temperature_c) {
        return SafetyFault::Overtemperature;
    }
    const std::uint32_t elapsed_ms = now_ms - previous_sample_ms;
    if (has_previous_sample && elapsed_ms > 0
        && std::abs(temperature_c - previous_temperature_c) * 1000.0F
                   / static_cast<float>(elapsed_ms)
               > config_.maximum_temperature_change_c_per_s) {
        return SafetyFault::ImplausibleTemperatureChange;
    }
    return SafetyFault::None;
}

SafetyFault SafetyController::check_staleness(const std::uint32_t now_ms,
                                              const std::uint32_t last_sample_ms) const noexcept {
    return now_ms - last_sample_ms > config_.temperature_timeout_ms
               ? SafetyFault::StaleTemperature
               : SafetyFault::None;
}

const char* safety_fault_name(const SafetyFault fault) noexcept {
    switch (fault) {
        case SafetyFault::None:
            return "none";
        case SafetyFault::InvalidTemperature:
            return "invalid temperature";
        case SafetyFault::Overtemperature:
            return "overtemperature";
        case SafetyFault::ImplausibleTemperatureChange:
            return "implausible temperature change";
        case SafetyFault::StaleTemperature:
            return "stale temperature";
    }
    return "unknown safety fault";
}

}  // namespace reflowCtrl
