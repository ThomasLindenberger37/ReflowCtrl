#ifndef REFLOWCTRL_BUTTON_DEBOUNCER_HPP
#define REFLOWCTRL_BUTTON_DEBOUNCER_HPP

#include <cstdint>

namespace reflowCtrl {

class ButtonDebouncer {
   public:
    explicit ButtonDebouncer(const std::uint32_t stable_sample_count) noexcept
        : stable_sample_count_(stable_sample_count) {}

    // Returns true only once when a stable released-to-pressed transition occurs.
    [[nodiscard]] bool update(const bool is_pressed) noexcept {
        if (!initialized_) {
            initialized_ = true;
            candidate_pressed_ = is_pressed;
            stable_pressed_ = is_pressed;
            sample_count_ = stable_sample_count_;
            return false;
        }

        if (is_pressed != candidate_pressed_) {
            candidate_pressed_ = is_pressed;
            sample_count_ = 1;
            return false;
        }

        if (sample_count_ < stable_sample_count_) {
            ++sample_count_;
        }

        if (sample_count_ == stable_sample_count_ && stable_pressed_ != candidate_pressed_) {
            stable_pressed_ = candidate_pressed_;
            return stable_pressed_;
        }

        return false;
    }

   private:
    const std::uint32_t stable_sample_count_;
    std::uint32_t sample_count_ = 0;
    bool initialized_ = false;
    bool candidate_pressed_ = false;
    bool stable_pressed_ = false;
};

}  // namespace reflowCtrl

#endif  // REFLOWCTRL_BUTTON_DEBOUNCER_HPP
