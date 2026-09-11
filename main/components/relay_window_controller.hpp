#ifndef REFLOWCTRL_RELAY_WINDOW_CONTROLLER_HPP
#define REFLOWCTRL_RELAY_WINDOW_CONTROLLER_HPP

#include <cstdint>

#include "components/temperature_controller.hpp"

namespace reflowCtrl {

class RelayWindowController {
   public:
    static constexpr std::uint32_t MINIMUM_RELAY_DWELL_MS = 3'000;
    static constexpr std::uint32_t MODULATION_PERIOD_MS = 12'000;

    void start(std::uint32_t now_ms) noexcept;
    void request(HeaterPower power) noexcept;
    [[nodiscard]] bool update(std::uint32_t now_ms) noexcept;
    void safety_off(std::uint32_t now_ms) noexcept;

    [[nodiscard]] HeaterPower requested_power() const noexcept {
        return requested_power_;
    }
    [[nodiscard]] HeaterPower active_power() const noexcept {
        return active_power_;
    }
    [[nodiscard]] bool relay_enabled() const noexcept {
        return relay_enabled_;
    }
    [[nodiscard]] std::uint32_t window_progress_ms(std::uint32_t now_ms) const noexcept;

   private:
    [[nodiscard]] bool requested_relay_state(std::uint32_t now_ms) const noexcept;

    std::uint32_t modulation_started_at_ms_ = 0;
    std::uint32_t last_transition_at_ms_ = 0;
    HeaterPower requested_power_ = HeaterPower::Off;
    HeaterPower active_power_ = HeaterPower::Off;
    bool relay_enabled_ = false;
    bool has_transitioned_ = false;
};

}  // namespace reflowCtrl

#endif  // REFLOWCTRL_RELAY_WINDOW_CONTROLLER_HPP
