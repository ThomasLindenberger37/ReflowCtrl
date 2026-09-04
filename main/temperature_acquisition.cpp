#include "temperature_acquisition.hpp"

#include "esp_check.h"
#include "esp_log.h"

namespace reflowCtrl {
namespace {

constexpr char TAG[] = "temperature";
constexpr TickType_t SAMPLE_PERIOD = pdMS_TO_TICKS(250);
constexpr std::uint32_t SAMPLES_PER_LOG = 4;
constexpr std::uint32_t TASK_STACK_SIZE = 4096;
constexpr UBaseType_t TASK_PRIORITY = 5;

}  // namespace

esp_err_t TemperatureAcquisition::start() noexcept {
    if (task_handle_ != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(sensor_.initialize(), TAG, "MAX6675 initialization failed");

    const BaseType_t task_created =
        xTaskCreate(task_entry, "temperature", TASK_STACK_SIZE, this, TASK_PRIORITY, &task_handle_);
    if (task_created != pdPASS) {
        task_handle_ = nullptr;
        ESP_LOGE(TAG, "Failed to create temperature task");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void TemperatureAcquisition::task_entry(void* const context) {
    static_cast<TemperatureAcquisition*>(context)->task_loop();
}

void TemperatureAcquisition::task_loop() noexcept {
    TickType_t last_wake_time = xTaskGetTickCount();

    // CS is high after initialization, which starts the first MAX6675 conversion.
    vTaskDelayUntil(&last_wake_time, SAMPLE_PERIOD);

    while (true) {
        acquire_sample();
        vTaskDelayUntil(&last_wake_time, SAMPLE_PERIOD);
    }
}

void TemperatureAcquisition::acquire_sample() noexcept {
    float raw_temperature_celsius = 0.0F;
    std::uint16_t raw_frame = 0;
    const esp_err_t result = sensor_.read_celsius(raw_temperature_celsius, raw_frame);

    if (result != ESP_OK) {
        publish_error(result);
        return;
    }

    filter_.add_sample(raw_temperature_celsius);
    const float filtered_temperature_celsius = filter_.filtered_temperature();
    publish_temperature(filtered_temperature_celsius);

    ++successful_sample_count_;
    if (successful_sample_count_ % SAMPLES_PER_LOG == 0) {
        ESP_LOGI(TAG, "Temperature: %.2f C", static_cast<double>(filtered_temperature_celsius));
    }
}

void TemperatureAcquisition::publish_temperature(const float temperature_celsius) noexcept {
    if (sensor_failed_) {
        ESP_LOGI(TAG, "MAX6675 readings recovered");
        sensor_failed_ = false;
    }
    bus_.publish(TemperatureMeasured{temperature_celsius});
}

void TemperatureAcquisition::publish_error(const esp_err_t error) noexcept {
    if (!sensor_failed_) {
        sensor_failed_ = true;
        ESP_LOGW(TAG, "MAX6675 read failed: %s", esp_err_to_name(error));
        bus_.publish(TemperatureSensorFailed{});
    }
}

}  // namespace reflowCtrl
