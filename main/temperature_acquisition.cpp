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
    TemperatureCallback callback = nullptr;
    void* callback_context = nullptr;
    bool recovered_from_sensor_error = false;

    portENTER_CRITICAL(&lock_);
    recovered_from_sensor_error = status_ == TemperatureStatus::SensorError;
    latest_temperature_celsius_ = temperature_celsius;
    status_ = TemperatureStatus::Valid;
    callback = callback_;
    callback_context = callback_context_;
    last_sensor_error_ = ESP_OK;
    portEXIT_CRITICAL(&lock_);

    if (recovered_from_sensor_error) {
        ESP_LOGI(TAG, "MAX6675 readings recovered");
    }

    if (callback != nullptr) {
        callback(temperature_celsius, callback_context);
    }
}

void TemperatureAcquisition::publish_error(const esp_err_t error) noexcept {
    bool should_log_error = false;

    portENTER_CRITICAL(&lock_);
    should_log_error = status_ != TemperatureStatus::SensorError || last_sensor_error_ != error;
    status_ = TemperatureStatus::SensorError;
    last_sensor_error_ = error;
    portEXIT_CRITICAL(&lock_);

    if (should_log_error) {
        ESP_LOGW(TAG, "MAX6675 read failed: %s", esp_err_to_name(error));
    }
}

}  // namespace reflowCtrl
