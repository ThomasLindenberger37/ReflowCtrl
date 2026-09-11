#include "components/relay_window_controller.hpp"

namespace reflowCtrl {

void RelayWindowController::start(const std::uint32_t now_ms) noexcept {
    modulation_started_at_ms_ = now_ms;
    last_transition_at_ms_ = now_ms;
    requested_power_ = HeaterPower::Off;
    active_power_ = HeaterPower::Off;
    relay_enabled_ = false;
    has_transitioned_ = false;
}

void RelayWindowController::request(const HeaterPower power) noexcept {
    requested_power_ = power;
}

bool RelayWindowController::update(const std::uint32_t now_ms) noexcept {
    active_power_ = requested_power_;
    const bool requested_state = requested_relay_state(now_ms);
    if (requested_state == relay_enabled_) {
        return relay_enabled_;
    }
    if (has_transitioned_ && now_ms - last_transition_at_ms_ < MINIMUM_RELAY_DWELL_MS) {
        return relay_enabled_;
    }
    relay_enabled_ = requested_state;
    last_transition_at_ms_ = now_ms;
    has_transitioned_ = true;
    return relay_enabled_;
}

void RelayWindowController::safety_off(const std::uint32_t now_ms) noexcept {
    modulation_started_at_ms_ = now_ms;
    last_transition_at_ms_ = now_ms;
    requested_power_ = HeaterPower::Off;
    active_power_ = HeaterPower::Off;
    relay_enabled_ = false;
    has_transitioned_ = true;
}

std::uint32_t RelayWindowController::window_progress_ms(const std::uint32_t now_ms) const noexcept {
    return (now_ms - modulation_started_at_ms_) % MODULATION_PERIOD_MS;
}

bool RelayWindowController::requested_relay_state(const std::uint32_t now_ms) const noexcept {
    const std::uint32_t on_time_ms = static_cast<std::uint32_t>(
        heater_power_percent(requested_power_) * MODULATION_PERIOD_MS / 100U);
    return window_progress_ms(now_ms) < on_time_ms;
}

}  // namespace reflowCtrl
