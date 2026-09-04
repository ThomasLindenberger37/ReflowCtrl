#include "max6675.hpp"

#include <cstdint>

#include "esp_check.h"
#include "esp_rom_sys.h"
#include "max6675_frame.hpp"

namespace reflowCtrl {
namespace {

// Deliberately slow software SPI: 50 us high and 50 us low gives about 10 kHz.
constexpr std::uint32_t CLOCK_DELAY_US = 50;

}  // namespace

esp_err_t Max6675::initialize() noexcept {
    const gpio_config_t output_config = {
        .pin_bit_mask = (1ULL << SCK_GPIO) | (1ULL << CS_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&output_config), "max6675",
                        "Failed to configure clock or chip select");

    const gpio_config_t input_config = {
        .pin_bit_mask = 1ULL << SO_MISO_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&input_config), "max6675", "Failed to configure SO/MISO");

    ESP_RETURN_ON_ERROR(gpio_set_level(CS_GPIO, 1), "max6675", "Failed to deselect sensor");
    return gpio_set_level(SCK_GPIO, 1);
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

    gpio_set_level(CS_GPIO, 0);
    esp_rom_delay_us(CLOCK_DELAY_US);

    for (std::uint8_t bit = 0; bit < 16; ++bit) {
        gpio_set_level(SCK_GPIO, 0);
        esp_rom_delay_us(CLOCK_DELAY_US);
        const auto input_bit = static_cast<std::uint16_t>(gpio_get_level(SO_MISO_GPIO));
        frame = static_cast<std::uint16_t>((frame << 1U) | input_bit);
        gpio_set_level(SCK_GPIO, 1);
        esp_rom_delay_us(CLOCK_DELAY_US);
    }

    gpio_set_level(CS_GPIO, 1);
    return frame;
}

}  // namespace reflowCtrl
