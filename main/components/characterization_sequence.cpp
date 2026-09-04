#include "components/characterization_sequence.hpp"

#include <cmath>

namespace reflowCtrl {

void CharacterizationSequence::start(const std::uint32_t now_ms) noexcept {
    started_at_ms_ = now_ms;
    stop_reason_ = CharacterizationStopReason::none;
    transition(CharacterizationPhase::baseline, now_ms, false);
}

void CharacterizationSequence::abort() noexcept {
    stop_reason_ = CharacterizationStopReason::user_abort;
    heater_enabled_ = false;
    phase_ = CharacterizationPhase::aborted;
}

void CharacterizationSequence::fail(const CharacterizationStopReason reason) noexcept {
    stop_reason_ = reason;
    heater_enabled_ = false;
    phase_ = CharacterizationPhase::error;
}

void CharacterizationSequence::update(const std::uint32_t now_ms,
                                      const float temperature_celsius) noexcept {
    if (!is_running()) {
        return;
    }
    if (!std::isfinite(temperature_celsius)) {
        fail(CharacterizationStopReason::invalid_temperature);
        return;
    }
    if (temperature_celsius >= SAFETY_TEMPERATURE_C) {
        fail(CharacterizationStopReason::overtemperature);
        return;
    }
    if (elapsed_ms(now_ms) >= MAX_DURATION_MS) {
        fail(CharacterizationStopReason::timeout);
        return;
    }

    switch (phase_) {
        case CharacterizationPhase::baseline:
            if (phase_elapsed(now_ms, BASELINE_DURATION_MS)) {
                transition(CharacterizationPhase::heat_1, now_ms, true);
            }
            break;
        case CharacterizationPhase::heat_1:
            if (temperature_celsius >= HEAT_1_TARGET_C) {
                transition(CharacterizationPhase::coast_1, now_ms, false);
            }
            break;
        case CharacterizationPhase::coast_1:
            if (phase_elapsed(now_ms, COAST_DURATION_MS)) {
                transition(CharacterizationPhase::heat_2, now_ms, true);
            }
            break;
        case CharacterizationPhase::heat_2:
            if (temperature_celsius >= HEAT_2_TARGET_C) {
                transition(CharacterizationPhase::coast_2, now_ms, false);
            }
            break;
        case CharacterizationPhase::coast_2:
            if (phase_elapsed(now_ms, COAST_DURATION_MS)) {
                transition(CharacterizationPhase::heat_3, now_ms, true);
            }
            break;
        case CharacterizationPhase::heat_3:
            if (temperature_celsius >= HEAT_3_TARGET_C) {
                transition(CharacterizationPhase::coast_3, now_ms, false);
            }
            break;
        case CharacterizationPhase::coast_3:
            if (phase_elapsed(now_ms, COAST_DURATION_MS)) {
                transition(CharacterizationPhase::heat_4, now_ms, true);
            }
            break;
        case CharacterizationPhase::heat_4:
            if (temperature_celsius >= HEAT_4_TARGET_C) {
                transition(CharacterizationPhase::coast_4, now_ms, false);
            }
            break;
        case CharacterizationPhase::coast_4:
            if (phase_elapsed(now_ms, FINAL_COAST_DURATION_MS)) {
                transition(CharacterizationPhase::final_heat, now_ms, true);
            }
            break;
        case CharacterizationPhase::final_heat:
            if (temperature_celsius >= FINAL_TARGET_C) {
                transition(CharacterizationPhase::cooldown, now_ms, false);
            }
            break;
        case CharacterizationPhase::cooldown:
            if (temperature_celsius <= COOLDOWN_COMPLETE_C) {
                transition(CharacterizationPhase::completed, now_ms, false);
            }
            break;
        case CharacterizationPhase::idle:
        case CharacterizationPhase::completed:
        case CharacterizationPhase::aborted:
        case CharacterizationPhase::error:
            break;
    }
}

bool CharacterizationSequence::is_running() const noexcept {
    return phase_ >= CharacterizationPhase::baseline && phase_ <= CharacterizationPhase::cooldown;
}

std::uint32_t CharacterizationSequence::elapsed_ms(const std::uint32_t now_ms) const noexcept {
    return is_running() || phase_ == CharacterizationPhase::completed
                   || phase_ == CharacterizationPhase::aborted
                   || phase_ == CharacterizationPhase::error
               ? now_ms - started_at_ms_
               : 0;
}

void CharacterizationSequence::transition(const CharacterizationPhase phase,
                                          const std::uint32_t now_ms,
                                          const bool heater_enabled) noexcept {
    phase_ = phase;
    phase_started_at_ms_ = now_ms;
    heater_enabled_ = heater_enabled;
}

bool CharacterizationSequence::phase_elapsed(const std::uint32_t now_ms,
                                             const std::uint32_t duration_ms) const noexcept {
    return now_ms - phase_started_at_ms_ >= duration_ms;
}

const char* characterization_phase_name(const CharacterizationPhase phase) noexcept {
    switch (phase) {
        case CharacterizationPhase::idle:
            return "idle";
        case CharacterizationPhase::baseline:
            return "baseline";
        case CharacterizationPhase::heat_1:
            return "heat_1";
        case CharacterizationPhase::coast_1:
            return "coast_1";
        case CharacterizationPhase::heat_2:
            return "heat_2";
        case CharacterizationPhase::coast_2:
            return "coast_2";
        case CharacterizationPhase::heat_3:
            return "heat_3";
        case CharacterizationPhase::coast_3:
            return "coast_3";
        case CharacterizationPhase::heat_4:
            return "heat_4";
        case CharacterizationPhase::coast_4:
            return "coast_4";
        case CharacterizationPhase::final_heat:
            return "final_heat";
        case CharacterizationPhase::cooldown:
            return "cooldown";
        case CharacterizationPhase::completed:
            return "completed";
        case CharacterizationPhase::aborted:
            return "aborted";
        case CharacterizationPhase::error:
            return "error";
    }
    return "error";
}

const char* characterization_stop_reason_name(const CharacterizationStopReason reason) noexcept {
    switch (reason) {
        case CharacterizationStopReason::none:
            return "";
        case CharacterizationStopReason::user_abort:
            return "user_abort";
        case CharacterizationStopReason::sensor_error:
            return "sensor_error";
        case CharacterizationStopReason::invalid_temperature:
            return "invalid_temperature";
        case CharacterizationStopReason::overtemperature:
            return "overtemperature";
        case CharacterizationStopReason::timeout:
            return "timeout";
        case CharacterizationStopReason::ota_update:
            return "ota_update";
    }
    return "unknown";
}

}  // namespace reflowCtrl
