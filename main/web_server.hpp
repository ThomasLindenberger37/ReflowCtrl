#pragma once

#include <atomic>

#include "esp_err.h"
#include "message_bus.hpp"

namespace reflowCtrl {

class WebServer {
   public:
    explicit WebServer(MessageBus& bus) noexcept : bus_(bus) {}

    esp_err_t start();
    void on_temperature_measured(const TemperatureMeasured& message) noexcept;
    void on_temperature_sensor_failed(const TemperatureSensorFailed&) noexcept;
    [[nodiscard]] float temperature_celsius() const noexcept {
        return temperature_celsius_.load();
    }
    [[nodiscard]] bool has_temperature() const noexcept {
        return has_temperature_.load();
    }
    void publish_ota_request(const OtaUpdateRequested& request) noexcept {
        bus_.publish(request);
    }

   private:
    MessageBus& bus_;
    std::atomic<float> temperature_celsius_{0.0F};
    std::atomic<bool> has_temperature_{false};
};

}  // namespace reflowCtrl
