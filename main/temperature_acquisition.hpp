#pragma once

#include <cstdint>
#include <optional>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "max6675.hpp"
#include "temperature_filter.hpp"

namespace reflowCtrl {

enum class TemperatureStatus : std::uint8_t {
    NoMeasurement,
    Valid,
    SensorError,
};

using TemperatureCallback = void (*)(float temperature_celsius, void* context);

class TemperatureAcquisition {
   public:
    TemperatureAcquisition() = default;

    TemperatureAcquisition(const TemperatureAcquisition&) = delete;
    TemperatureAcquisition& operator=(const TemperatureAcquisition&) = delete;

    esp_err_t start() noexcept;

    [[nodiscard]] std::optional<float> temperature_celsius() const noexcept;
    [[nodiscard]] TemperatureStatus status() const noexcept;

    // Called from the temperature task after a successful 250 ms sample.
    // The callback must return quickly and must not block the sampling schedule.
    void set_callback(TemperatureCallback callback, void* context = nullptr) noexcept;

   private:
    static void task_entry(void* context);
    void task_loop() noexcept;
    void acquire_sample() noexcept;
    void publish_temperature(float temperature_celsius) noexcept;
    void publish_error(esp_err_t error) noexcept;

    Max6675 sensor_;
    GaussianTemperatureFilter filter_;
    TaskHandle_t task_handle_ = nullptr;
    mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
    float latest_temperature_celsius_ = 0.0F;
    TemperatureStatus status_ = TemperatureStatus::NoMeasurement;
    TemperatureCallback callback_ = nullptr;
    void* callback_context_ = nullptr;
    esp_err_t last_sensor_error_ = ESP_OK;
    std::uint32_t successful_sample_count_ = 0;
};

}  // namespace reflowCtrl
