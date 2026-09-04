#ifndef REFLOWCTRL_CHARACTERIZATION_SEQUENCE_HPP
#define REFLOWCTRL_CHARACTERIZATION_SEQUENCE_HPP

#include <cstdint>

#include "messages.hpp"

namespace reflowCtrl {

enum class CharacterizationStopReason : std::uint8_t {
    none,
    user_abort,
    sensor_error,
    invalid_temperature,
    overtemperature,
    timeout,
    ota_update,
};

class CharacterizationSequence {
   public:
    static constexpr std::uint32_t BASELINE_DURATION_MS = 30'000;
    static constexpr std::uint32_t COAST_DURATION_MS = 20'000;
    static constexpr std::uint32_t FINAL_COAST_DURATION_MS = 30'000;
    static constexpr std::uint32_t MAX_DURATION_MS = 20 * 60 * 1000;
    static constexpr float HEAT_1_TARGET_C = 80.0F;
    static constexpr float HEAT_2_TARGET_C = 130.0F;
    static constexpr float HEAT_3_TARGET_C = 180.0F;
    static constexpr float HEAT_4_TARGET_C = 220.0F;
    static constexpr float FINAL_TARGET_C = 240.0F;
    static constexpr float SAFETY_TEMPERATURE_C = 250.0F;
    static constexpr float COOLDOWN_COMPLETE_C = 50.0F;

    void start(std::uint32_t now_ms) noexcept;
    void abort() noexcept;
    void fail(CharacterizationStopReason reason) noexcept;
    void update(std::uint32_t now_ms, float temperature_celsius) noexcept;

    [[nodiscard]] CharacterizationPhase phase() const noexcept {
        return phase_;
    }
    [[nodiscard]] CharacterizationStopReason stop_reason() const noexcept {
        return stop_reason_;
    }
    [[nodiscard]] bool heater_enabled() const noexcept {
        return heater_enabled_;
    }
    [[nodiscard]] bool is_running() const noexcept;
    [[nodiscard]] std::uint32_t elapsed_ms(std::uint32_t now_ms) const noexcept;

   private:
    void transition(CharacterizationPhase phase, std::uint32_t now_ms,
                    bool heater_enabled) noexcept;
    [[nodiscard]] bool phase_elapsed(std::uint32_t now_ms,
                                     std::uint32_t duration_ms) const noexcept;

    CharacterizationPhase phase_ = CharacterizationPhase::idle;
    CharacterizationStopReason stop_reason_ = CharacterizationStopReason::none;
    std::uint32_t started_at_ms_ = 0;
    std::uint32_t phase_started_at_ms_ = 0;
    bool heater_enabled_ = false;
};

[[nodiscard]] const char* characterization_phase_name(CharacterizationPhase phase) noexcept;
[[nodiscard]] const char* characterization_stop_reason_name(
    CharacterizationStopReason reason) noexcept;

}  // namespace reflowCtrl

#endif  // REFLOWCTRL_CHARACTERIZATION_SEQUENCE_HPP
