#include "components/relay_window_controller.hpp"

namespace reflowCtrl {

void RelayWindowController::start(const std::uint32_t now_ms) noexcept {
    window_started_at_ms_ = now_ms;
    requested_power_ = HeaterPower::Off;
    active_power_ = HeaterPower::Off;
    relay_enabled_ = false;
}

void RelayWindowController::request(const HeaterPower power) noexcept {
    requested_power_ = power;
}

bool RelayWindowController::update(const std::uint32_t now_ms) noexcept {
    const std::uint32_t elapsed = now_ms - window_started_at_ms_;
    if (elapsed >= WINDOW_MS) {
        window_started_at_ms_ += (elapsed / WINDOW_MS) * WINDOW_MS;
        active_power_ = requested_power_;
    }
    const std::uint32_t on_time_ms =
        static_cast<std::uint32_t>(heater_power_percent(active_power_)) * WINDOW_MS / 100U;
    relay_enabled_ = window_progress_ms(now_ms) < on_time_ms;
    return relay_enabled_;
}

void RelayWindowController::safety_off(const std::uint32_t now_ms) noexcept {
    window_started_at_ms_ = now_ms;
    requested_power_ = HeaterPower::Off;
    active_power_ = HeaterPower::Off;
    relay_enabled_ = false;
}

std::uint32_t RelayWindowController::window_progress_ms(const std::uint32_t now_ms) const noexcept {
    return (now_ms - window_started_at_ms_) % WINDOW_MS;
}

}  // namespace reflowCtrl
