#include "temperature_acquisition.hpp"

#include "esp_check.h"
#include "esp_log.h"

namespace reflow_pilot {
namespace {

constexpr char TAG[] = "temperature";
constexpr TickType_t SAMPLE_PERIOD = pdMS_TO_TICKS(250);
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

std::optional<float> TemperatureAcquisition::temperature_celsius() const noexcept {
    portENTER_CRITICAL(&lock_);
    const bool has_valid_temperature = status_ == TemperatureStatus::Valid;
    const float temperature = latest_temperature_celsius_;
    portEXIT_CRITICAL(&lock_);

    if (!has_valid_temperature) {
        return std::nullopt;
    }

    return std::optional<float>{temperature};
}

TemperatureStatus TemperatureAcquisition::status() const noexcept {
    portENTER_CRITICAL(&lock_);
    const TemperatureStatus current_status = status_;
    portEXIT_CRITICAL(&lock_);
    return current_status;
}

void TemperatureAcquisition::set_callback(const TemperatureCallback callback,
                                          void* const context) noexcept {
    portENTER_CRITICAL(&lock_);
    callback_ = callback;
    callback_context_ = context;
    portEXIT_CRITICAL(&lock_);
}

void TemperatureAcquisition::task_entry(void* const context) {
    static_cast<TemperatureAcquisition*>(context)->task_loop();
}

void TemperatureAcquisition::task_loop() noexcept {
    TickType_t last_wake_time = xTaskGetTickCount();

    // CS is high after initialization, which starts the first MAX6675 conversion.
    // Do not interrupt it before the 250 ms conversion interval has elapsed.
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
        publish_error();
        return;
    }

    filter_.add_sample(raw_temperature_celsius);
    const float filtered_temperature_celsius = filter_.filtered_temperature();
    ESP_LOGI(TAG, "MAX6675 frame: 0x%04X, raw: %.2f C, filtered: %.2f C",
             static_cast<unsigned int>(raw_frame), static_cast<double>(raw_temperature_celsius),
             static_cast<double>(filtered_temperature_celsius));
    publish_temperature(filtered_temperature_celsius);
}

void TemperatureAcquisition::publish_temperature(const float temperature_celsius) noexcept {
    TemperatureCallback callback = nullptr;
    void* callback_context = nullptr;

    portENTER_CRITICAL(&lock_);
    latest_temperature_celsius_ = temperature_celsius;
    status_ = TemperatureStatus::Valid;
    callback = callback_;
    callback_context = callback_context_;
    portEXIT_CRITICAL(&lock_);

    if (callback != nullptr) {
        callback(temperature_celsius, callback_context);
    }
}

void TemperatureAcquisition::publish_error() noexcept {
    portENTER_CRITICAL(&lock_);
    status_ = TemperatureStatus::SensorError;
    portEXIT_CRITICAL(&lock_);
}

}  // namespace reflow_pilot
