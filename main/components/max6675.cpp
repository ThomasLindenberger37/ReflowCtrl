#include "max6675.hpp"

#include <cstdint>

#include "esp_rom_sys.h"
#include "max6675_frame.hpp"

namespace reflowCtrl {
namespace {

// Deliberately slow software SPI: 50 us high and 50 us low gives about 10 kHz.
constexpr std::uint32_t CLOCK_DELAY_US = 50;

}  // namespace

esp_err_t Max6675::initialize() noexcept {
    chip_select_.set(true);
    clock_.set(true);
    return ESP_OK;
}

esp_err_t Max6675::read_celsius(float& temperature_celsius, std::uint16_t& raw_frame) noexcept {
    raw_frame = read_frame();
    const Max6675FrameResult decoded_frame = decode_max6675_frame(raw_frame);

    if (decoded_frame.status == Max6675FrameStatus::Invalid) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (decoded_frame.status == Max6675FrameStatus::ThermocoupleOpen) {
        return ESP_ERR_INVALID_STATE;
    }

    temperature_celsius = decoded_frame.temperature_celsius;
    return ESP_OK;
}

std::uint16_t Max6675::read_frame() noexcept {
    std::uint16_t frame = 0;

    chip_select_.set(false);
    esp_rom_delay_us(CLOCK_DELAY_US);

    for (std::uint8_t bit = 0; bit < 16; ++bit) {
        clock_.set(false);
        esp_rom_delay_us(CLOCK_DELAY_US);
        const auto input_bit = static_cast<std::uint16_t>(serial_output_.is_active());
        frame = static_cast<std::uint16_t>((frame << 1U) | input_bit);
        clock_.set(true);
        esp_rom_delay_us(CLOCK_DELAY_US);
    }

    chip_select_.set(true);
    return frame;
}

}  // namespace reflowCtrl
