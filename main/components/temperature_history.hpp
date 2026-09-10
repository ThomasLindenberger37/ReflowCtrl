#ifndef REFLOWCTRL_TEMPERATURE_HISTORY_HPP
#define REFLOWCTRL_TEMPERATURE_HISTORY_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace reflowCtrl {

class TemperatureHistory {
   public:
    void reset() noexcept;
    void add(std::uint32_t now_ms, float temperature_c) noexcept;
    [[nodiscard]] float ramp_c_per_s() const noexcept;

   private:
    struct Sample {
        std::uint32_t time_ms = 0;
        float temperature_c = 0.0F;
    };
    static constexpr std::size_t CAPACITY = 10;
    std::array<Sample, CAPACITY> samples_{};
    std::size_t next_ = 0;
    std::size_t count_ = 0;
};

}  // namespace reflowCtrl

#endif  // REFLOWCTRL_TEMPERATURE_HISTORY_HPP
