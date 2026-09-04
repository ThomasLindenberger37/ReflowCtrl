#include "components/characterization_controller.hpp"

#include "esp_log.h"
#include "esp_timer.h"

namespace reflowCtrl {
namespace {

constexpr char TAG[] = "characterization";

}  // namespace

esp_err_t CharacterizationController::start() noexcept {
    mutex_ = xSemaphoreCreateMutexStatic(&mutex_storage_);
    if (mutex_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    return bus_.subscribe<CharacterizationStarted>(
               &CharacterizationController::on_characterization_started, this)
                   && bus_.subscribe<CharacterizationAborted>(
                       &CharacterizationController::on_characterization_aborted, this)
                   && bus_.subscribe<TemperatureMeasured>(
                       &CharacterizationController::on_temperature_measured, this)
                   && bus_.subscribe<TemperatureSensorFailed>(
                       &CharacterizationController::on_temperature_sensor_failed, this)
                   && bus_.subscribe<OtaUpdateStarted>(
                       &CharacterizationController::on_ota_update_started, this)
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}

void CharacterizationController::on_characterization_started(
    const CharacterizationStarted&) noexcept {
    lock();
    if (sequence_.is_running()) {
        unlock();
        return;
    }
    live_feed_.reset();
    sequence_.start(now_ms());
    unlock();
    publish_heater_output(false);
    ESP_LOGI(TAG, "Characterization started");
}

void CharacterizationController::on_characterization_aborted(
    const CharacterizationAborted&) noexcept {
    lock();
    if (!sequence_.is_running()) {
        unlock();
        return;
    }
    sequence_.abort();
    unlock();
    publish_heater_output(false);
    ESP_LOGW(TAG, "Characterization aborted by user");
}

void CharacterizationController::on_temperature_measured(
    const TemperatureMeasured& message) noexcept {
    last_temperature_celsius_.store(message.temperature_celsius);
    has_temperature_.store(true);

    lock();
    const bool was_running = sequence_.is_running();
    const bool previous_heater_output = sequence_.heater_enabled();
    const CharacterizationPhase previous_phase = sequence_.phase();
    const std::uint32_t time_ms = now_ms();
    sequence_.update(time_ms, message.temperature_celsius);
    const CharacterizationPhase phase = sequence_.phase();
    if (was_running) {
        live_feed_.append(sequence_.elapsed_ms(time_ms), message.temperature_celsius,
                          sequence_.heater_enabled(), phase);
    }
    const bool heater_output_changed = previous_heater_output != sequence_.heater_enabled();
    const bool heater_output = sequence_.heater_enabled();
    const CharacterizationStopReason reason = sequence_.stop_reason();
    const bool stopped = was_running && !sequence_.is_running();
    const bool stop_reason_changed = stopped && previous_phase != sequence_.phase();
    unlock();

    if (heater_output_changed || stopped) {
        publish_heater_output(heater_output);
    }
    if (stop_reason_changed && reason != CharacterizationStopReason::none) {
        ESP_LOGW(TAG, "Characterization stopped: %s", characterization_stop_reason_name(reason));
    }
}

void CharacterizationController::on_temperature_sensor_failed(
    const TemperatureSensorFailed&) noexcept {
    has_temperature_.store(false);
    stop_with_error(CharacterizationStopReason::sensor_error);
}

void CharacterizationController::on_ota_update_started(const OtaUpdateStarted&) noexcept {
    stop_with_error(CharacterizationStopReason::ota_update);
}

CharacterizationStatus CharacterizationController::status() noexcept {
    lock();
    CharacterizationStatus result{};
    result.phase = sequence_.phase();
    result.stop_reason = sequence_.stop_reason();
    result.elapsed_ms = sequence_.elapsed_ms(now_ms());
    result.temperature_celsius = last_temperature_celsius_.load();
    result.has_temperature = has_temperature_.load();
    result.heater_output = sequence_.heater_enabled();
    unlock();
    return result;
}

CharacterizationLiveFeed::PreviewSnapshot CharacterizationController::preview_after(
    const std::uint32_t cursor) noexcept {
    return live_feed_.preview_after(cursor);
}

std::uint32_t CharacterizationController::now_ms() noexcept {
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
}

void CharacterizationController::publish_heater_output(const bool enabled) noexcept {
    bus_.publish(HeaterOutputRequested{enabled});
}

void CharacterizationController::stop_with_error(const CharacterizationStopReason reason) noexcept {
    lock();
    if (!sequence_.is_running()) {
        unlock();
        return;
    }
    sequence_.fail(reason);
    unlock();
    publish_heater_output(false);
    ESP_LOGE(TAG, "Characterization stopped: %s", characterization_stop_reason_name(reason));
}

void CharacterizationController::lock() noexcept {
    xSemaphoreTake(mutex_, portMAX_DELAY);
}

void CharacterizationController::unlock() noexcept {
    xSemaphoreGive(mutex_);
}

}  // namespace reflowCtrl
