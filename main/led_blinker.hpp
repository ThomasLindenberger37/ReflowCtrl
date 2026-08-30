#pragma once

#include <cstdint>

namespace reflow_pilot {

enum class LedBlinkMode : std::uint8_t {
    Fast,
    Slow,
    LongOnShortOff,
    LongOffShortOn,
    On,
    Off,
};

class LedBlinker {
   public:
    static constexpr std::uint32_t FAST_DURATION_MS = 200;
    static constexpr std::uint32_t SLOW_DURATION_MS = 1000;
    static constexpr std::uint32_t LONG_DURATION_MS = 1000;
    static constexpr std::uint32_t SHORT_DURATION_MS = 200;

    explicit LedBlinker(const LedBlinkMode mode = LedBlinkMode::Off) noexcept {
        set_mode(mode);
    }

    void set_mode(const LedBlinkMode mode) noexcept {
        mode_ = mode;
        phase_elapsed_ms_ = 0;
        is_on_ = mode != LedBlinkMode::Off && mode != LedBlinkMode::LongOffShortOn;
    }

    [[nodiscard]] LedBlinkMode mode() const noexcept {
        return mode_;
    }

    [[nodiscard]] bool is_on() const noexcept {
        return is_on_;
    }

    void advance(const std::uint32_t elapsed_ms) noexcept {
        if (mode_ == LedBlinkMode::On || mode_ == LedBlinkMode::Off) {
            return;
        }

        phase_elapsed_ms_ += elapsed_ms;

        while (phase_elapsed_ms_ >= current_phase_duration_ms()) {
            phase_elapsed_ms_ -= current_phase_duration_ms();
            is_on_ = !is_on_;
        }
    }

   private:
    [[nodiscard]] std::uint32_t current_phase_duration_ms() const noexcept {
        if (mode_ == LedBlinkMode::Fast) {
            return FAST_DURATION_MS;
        }

        if (mode_ == LedBlinkMode::Slow) {
            return SLOW_DURATION_MS;
        }

        const bool long_phase = (mode_ == LedBlinkMode::LongOnShortOff && is_on_)
                                || (mode_ == LedBlinkMode::LongOffShortOn && !is_on_);
        return long_phase ? LONG_DURATION_MS : SHORT_DURATION_MS;
    }

    LedBlinkMode mode_ = LedBlinkMode::Off;
    std::uint32_t phase_elapsed_ms_ = 0;
    bool is_on_ = false;
};

}  // namespace reflow_pilot
