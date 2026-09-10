#ifndef REFLOWCTRL_SAFETY_CONTROLLER_HPP
#define REFLOWCTRL_SAFETY_CONTROLLER_HPP

#include <cstdint>

namespace reflowCtrl {

enum class SafetyFault : std::uint8_t {
    None,
    InvalidTemperature,
    Overtemperature,
    ImplausibleTemperatureChange,
    StaleTemperature,
};

struct SafetyControllerConfig {
    float maximum_temperature_c = 250.0F;
    float maximum_temperature_change_c_per_s = 30.0F;
    std::uint32_t temperature_timeout_ms = 2'000;
};

class SafetyController {
   public:
    explicit SafetyController(SafetyControllerConfig config = {}) noexcept : config_(config) {}

    [[nodiscard]] SafetyFault check_sample(float temperature_c, std::uint32_t now_ms,
                                           bool has_previous_sample,
                                           float previous_temperature_c,
                                           std::uint32_t previous_sample_ms) const noexcept;
    [[nodiscard]] SafetyFault check_staleness(std::uint32_t now_ms,
                                              std::uint32_t last_sample_ms) const noexcept;

   private:
    SafetyControllerConfig config_;
};

[[nodiscard]] const char* safety_fault_name(SafetyFault fault) noexcept;

}  // namespace reflowCtrl

#endif  // REFLOWCTRL_SAFETY_CONTROLLER_HPP
