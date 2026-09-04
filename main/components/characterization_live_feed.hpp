#ifndef REFLOWCTRL_CHARACTERIZATION_LIVE_FEED_HPP
#define REFLOWCTRL_CHARACTERIZATION_LIVE_FEED_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "components/characterization_sequence.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace reflowCtrl {

class CharacterizationLiveFeed {
   public:
    // The HTTP server has a small task stack. At 200 ms sampling and 400 ms polling,
    // sixteen lines provide ample transfer headroom without risking a stack overflow.
    static constexpr std::size_t PREVIEW_CAPACITY = 16;
    static constexpr std::size_t LINE_SIZE = 80;

    struct PreviewSnapshot {
        std::array<std::array<char, LINE_SIZE>, PREVIEW_CAPACITY> lines{};
        std::size_t count = 0;
        std::uint32_t next_cursor = 0;
    };

    CharacterizationLiveFeed() noexcept;
    void reset() noexcept;
    void append(std::uint32_t time_ms, float temperature_celsius, bool heater_output,
                CharacterizationPhase phase) noexcept;
    [[nodiscard]] PreviewSnapshot preview_after(std::uint32_t cursor) noexcept;

   private:
    void lock() noexcept;
    void unlock() noexcept;
    void append_preview(const char* line) noexcept;

    StaticSemaphore_t mutex_storage_{};
    SemaphoreHandle_t mutex_ = nullptr;
    std::array<std::array<char, LINE_SIZE>, PREVIEW_CAPACITY> preview_{};
    std::uint32_t first_preview_id_ = 1;
    std::uint32_t next_preview_id_ = 1;
    std::size_t preview_count_ = 0;
};

}  // namespace reflowCtrl

#endif  // REFLOWCTRL_CHARACTERIZATION_LIVE_FEED_HPP
