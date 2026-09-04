#pragma once

#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "max6675.hpp"
#include "message_bus.hpp"
#include "temperature_filter.hpp"

namespace reflowCtrl {

class TemperatureAcquisition {
   public:
    TemperatureAcquisition(MessageBus& bus, Max6675& sensor) noexcept
        : sensor_(sensor), bus_(bus) {}

    TemperatureAcquisition(const TemperatureAcquisition&) = delete;
    TemperatureAcquisition& operator=(const TemperatureAcquisition&) = delete;

    esp_err_t start() noexcept;

   private:
    static void task_entry(void* context);
    void task_loop() noexcept;
    void acquire_sample() noexcept;
    void publish_temperature(float temperature_celsius) noexcept;
    void publish_error(esp_err_t error) noexcept;

    Max6675& sensor_;
    MessageBus& bus_;
    GaussianTemperatureFilter filter_;
    TaskHandle_t task_handle_ = nullptr;
    bool sensor_failed_ = false;
    std::uint32_t successful_sample_count_ = 0;
};

}  // namespace reflowCtrl
