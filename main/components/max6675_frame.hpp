#ifndef REFLOWCTRL_MAX6675_FRAME_HPP
#define REFLOWCTRL_MAX6675_FRAME_HPP

#include <cstdint>

namespace reflowCtrl {

enum class Max6675FrameStatus : std::uint8_t {
    Valid,
    ThermocoupleOpen,
    Invalid,
};

struct Max6675FrameResult {
    Max6675FrameStatus status;
    float temperature_celsius;
};

[[nodiscard]] constexpr Max6675FrameResult decode_max6675_frame(
    const std::uint16_t frame) noexcept {
    constexpr std::uint16_t THERMOCOUPLE_OPEN_BIT = 0x0004;
    constexpr std::uint16_t INVALID_BITS = 0x8002;

    if ((frame & INVALID_BITS) != 0) {
        return {Max6675FrameStatus::Invalid, 0.0F};
    }
    if ((frame & THERMOCOUPLE_OPEN_BIT) != 0) {
        return {Max6675FrameStatus::ThermocoupleOpen, 0.0F};
    }
    return {Max6675FrameStatus::Valid, static_cast<float>(frame >> 3U) * 0.25F};
}

}  // namespace reflowCtrl

#endif  // REFLOWCTRL_MAX6675_FRAME_HPP
