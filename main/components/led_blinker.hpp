#ifndef REFLOWCTRL_LED_BLINKER_HPP
#define REFLOWCTRL_LED_BLINKER_HPP

#include <atomic>
#include <cstdint>

namespace reflowCtrl {

enum class LedBlinkMode : std::uint8_t {
    TenHertz,
    Fast,
    Slow,
    LongOnShortOff,
    LongOffShortOn,
    On,
    Off,
};

class LedBlinker {
   public:
    static constexpr std::uint32_t TEN_HERTZ_PHASE_DURATION_MS = 50;
    static constexpr std::uint32_t FAST_DURATION_MS = 200;
    static constexpr std::uint32_t SLOW_DURATION_MS = 1000;
    static constexpr std::uint32_t LONG_DURATION_MS = 1000;
    static constexpr std::uint32_t SHORT_DURATION_MS = 200;

    explicit LedBlinker(const LedBlinkMode mode = LedBlinkMode::Off) noexcept {
        set_mode(mode);
    }

    void set_mode(const LedBlinkMode mode) noexcept {
        requested_mode_.store(mode);
        apply_mode(mode);
    }

    void request_mode(const LedBlinkMode mode) noexcept {
        requested_mode_.store(mode);
    }

    void apply_requested_mode() noexcept {
        const LedBlinkMode requested_mode = requested_mode_.load();
        if (requested_mode != mode_) {
            apply_mode(requested_mode);
        }
    }

    void advance(const std::uint32_t elapsed_ms) noexcept {
        apply_requested_mode();

        if (mode_ == LedBlinkMode::On || mode_ == LedBlinkMode::Off) {
            return;
        }

        phase_elapsed_ms_ += elapsed_ms;

        while (phase_elapsed_ms_ >= current_phase_duration_ms()) {
            phase_elapsed_ms_ -= current_phase_duration_ms();
            is_on_ = !is_on_;
        }
    }

    [[nodiscard]] LedBlinkMode mode() const noexcept {
        return mode_;
    }

    [[nodiscard]] bool is_on() const noexcept {
        return is_on_;
    }

   private:
    void apply_mode(const LedBlinkMode mode) noexcept {
        mode_ = mode;
        phase_elapsed_ms_ = 0;
        is_on_ = mode != LedBlinkMode::Off && mode != LedBlinkMode::LongOffShortOn;
    }

    [[nodiscard]] std::uint32_t current_phase_duration_ms() const noexcept {
        if (mode_ == LedBlinkMode::TenHertz) {
            return TEN_HERTZ_PHASE_DURATION_MS;
        }

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
    std::atomic<LedBlinkMode> requested_mode_{LedBlinkMode::Off};
    std::uint32_t phase_elapsed_ms_ = 0;
    bool is_on_ = false;
};

}  // namespace reflowCtrl

#endif  // REFLOWCTRL_LED_BLINKER_HPP
