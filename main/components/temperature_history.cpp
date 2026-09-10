#include "components/temperature_history.hpp"

namespace reflowCtrl {

void TemperatureHistory::reset() noexcept {
    next_ = 0;
    count_ = 0;
}

void TemperatureHistory::add(const std::uint32_t now_ms, const float temperature_c) noexcept {
    samples_[next_] = {now_ms, temperature_c};
    next_ = (next_ + 1) % CAPACITY;
    if (count_ < CAPACITY) {
        ++count_;
    }
}

float TemperatureHistory::ramp_c_per_s() const noexcept {
    if (count_ < 2) {
        return 0.0F;
    }
    const std::size_t oldest_index = count_ == CAPACITY ? next_ : 0;
    const std::size_t newest_index = (next_ + CAPACITY - 1) % CAPACITY;
    const Sample& oldest = samples_[oldest_index];
    const Sample& newest = samples_[newest_index];
    const std::uint32_t elapsed_ms = newest.time_ms - oldest.time_ms;
    return elapsed_ms == 0 ? 0.0F
                           : (newest.temperature_c - oldest.temperature_c) * 1000.0F
                                 / static_cast<float>(elapsed_ms);
}

}  // namespace reflowCtrl
