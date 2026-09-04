#ifndef REFLOWCTRL_CHARACTERIZATION_CONTROLLER_HPP
#define REFLOWCTRL_CHARACTERIZATION_CONTROLLER_HPP

#include <atomic>
#include <cstdint>

#include "components/characterization_live_feed.hpp"
#include "components/characterization_sequence.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "message_bus.hpp"

namespace reflowCtrl {

struct CharacterizationStatus {
    CharacterizationPhase phase = CharacterizationPhase::idle;
    CharacterizationStopReason stop_reason = CharacterizationStopReason::none;
    std::uint32_t elapsed_ms = 0;
    float temperature_celsius = 0.0F;
    bool has_temperature = false;
    bool heater_output = false;
};

class CharacterizationController {
   public:
    CharacterizationController(MessageBus& bus, CharacterizationLiveFeed& live_feed) noexcept
        : bus_(bus), live_feed_(live_feed) {}

    esp_err_t start() noexcept;
    void on_characterization_started(const CharacterizationStarted&) noexcept;
    void on_characterization_aborted(const CharacterizationAborted&) noexcept;
    void on_temperature_measured(const TemperatureMeasured& message) noexcept;
    void on_temperature_sensor_failed(const TemperatureSensorFailed&) noexcept;
    void on_ota_update_started(const OtaUpdateStarted&) noexcept;
    [[nodiscard]] CharacterizationStatus status() noexcept;
    [[nodiscard]] CharacterizationLiveFeed::PreviewSnapshot preview_after(
        std::uint32_t cursor) noexcept;

   private:
    [[nodiscard]] static std::uint32_t now_ms() noexcept;
    void publish_heater_output(bool enabled) noexcept;
    void stop_with_error(CharacterizationStopReason reason) noexcept;
    void lock() noexcept;
    void unlock() noexcept;

    MessageBus& bus_;
    CharacterizationLiveFeed& live_feed_;
    CharacterizationSequence sequence_;
    StaticSemaphore_t mutex_storage_{};
    SemaphoreHandle_t mutex_ = nullptr;
    std::atomic<float> last_temperature_celsius_{0.0F};
    std::atomic<bool> has_temperature_{false};
};

}  // namespace reflowCtrl

#endif  // REFLOWCTRL_CHARACTERIZATION_CONTROLLER_HPP
