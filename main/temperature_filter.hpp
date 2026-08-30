#pragma once

#include <array>
#include <cstddef>

namespace reflow_pilot {

class GaussianTemperatureFilter {
   public:
    static constexpr std::size_t TAP_COUNT = 4;

    void add_sample(const float temperature_celsius) noexcept {
        samples_[next_sample_index_] = temperature_celsius;
        next_sample_index_ = (next_sample_index_ + 1) % TAP_COUNT;

        if (sample_count_ < TAP_COUNT) {
            ++sample_count_;
        }
    }

    [[nodiscard]] bool has_samples() const noexcept {
        return sample_count_ > 0;
    }

    [[nodiscard]] float filtered_temperature() const noexcept {
        float weighted_sum = 0.0F;
        float used_weight_sum = 0.0F;

        for (std::size_t age = 0; age < sample_count_; ++age) {
            const std::size_t sample_index = (next_sample_index_ + TAP_COUNT - 1 - age) % TAP_COUNT;
            const float weight = GAUSSIAN_COEFFICIENTS[age];
            weighted_sum += samples_[sample_index] * weight;
            used_weight_sum += weight;
        }

        return weighted_sum / used_weight_sum;
    }

   private:
    // Symmetric 4-tap Gaussian approximation, ordered current sample to -750 ms.
    // The coefficients sum to 1.0 and retain 250 ms sample spacing.
    static constexpr std::array<float, TAP_COUNT> GAUSSIAN_COEFFICIENTS = {
        0.134F,
        0.366F,
        0.366F,
        0.134F,
    };

    std::array<float, TAP_COUNT> samples_{};
    std::size_t next_sample_index_ = 0;
    std::size_t sample_count_ = 0;
};

}  // namespace reflow_pilot
