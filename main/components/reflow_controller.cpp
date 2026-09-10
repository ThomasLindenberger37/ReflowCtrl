#include "components/reflow_controller.hpp"

#include <algorithm>
#include <cmath>

#include "esp_log.h"
#include "esp_timer.h"
#include "profile_storage.hpp"

namespace reflowCtrl {
namespace {

constexpr char TAG[] = "reflow";

struct TargetPoint {
    float temperature_c = 0.0F;
    float ramp_c_per_s = 0.0F;
};

TargetPoint target_at(const std::vector<CurvePoint>& curve, const double elapsed_s) {
    if (curve.empty()) {
        return {};
    }
    const auto upper = std::lower_bound(
        curve.begin(), curve.end(), elapsed_s,
        [](const CurvePoint& point, const double time) { return point.time_s < time; });
    if (upper == curve.begin()) {
        return {static_cast<float>(upper->temperature_c), 0.0F};
    }
    if (upper == curve.end()) {
        return {static_cast<float>(curve.back().temperature_c), 0.0F};
    }
    const CurvePoint& before = *std::prev(upper);
    const double duration = upper->time_s - before.time_s;
    if (duration <= 0.0) {
        return {static_cast<float>(upper->temperature_c), 0.0F};
    }
    const double progress = (elapsed_s - before.time_s) / duration;
    return {static_cast<float>(before.temperature_c
                               + (upper->temperature_c - before.temperature_c) * progress),
            static_cast<float>((upper->temperature_c - before.temperature_c) / duration)};
}

}  // namespace

esp_err_t ReflowController::start() noexcept {
    mutex_ = xSemaphoreCreateMutexStatic(&mutex_storage_);
    if (mutex_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    return bus_.subscribe<ReflowStarted>(&ReflowController::on_reflow_started, this)
                   && bus_.subscribe<ReflowAborted>(&ReflowController::on_reflow_aborted, this)
                   && bus_.subscribe<TemperatureMeasured>(
                       &ReflowController::on_temperature_measured, this)
                   && bus_.subscribe<TemperatureSensorFailed>(
                       &ReflowController::on_temperature_sensor_failed, this)
                   && bus_.subscribe<OtaUpdateStarted>(&ReflowController::on_ota_update_started,
                                                       this)
                   && bus_.subscribe<CharacterizationStarted>(
                       &ReflowController::on_characterization_started, this)
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}

void ReflowController::on_reflow_started(const ReflowStarted&) noexcept {
    lock();
    start_process(now_ms());
    unlock();
}

void ReflowController::start_process(const std::uint32_t time_ms) noexcept {
    if (telemetry_.state == ReflowState::Running) {
        return;
    }
    ProfileConfiguration profile;
    std::optional<OvenCapabilities> oven;
    if (!telemetry_.has_temperature || !load_active_profile_for_execution(profile, oven)) {
        stop(ReflowState::Fault, time_ms);
        ESP_LOGE(TAG, "Cannot start without a valid profile and temperature");
        return;
    }
    ProfilePreview preview = generate_profile_preview(profile, oven);
    if (!preview.valid || preview.points.empty()) {
        stop(ReflowState::Fault, time_ms);
        ESP_LOGE(TAG, "Active profile cannot be executed");
        return;
    }
    profile_ = std::move(profile);
    oven_ = std::move(oven);
    curve_ = std::move(preview.points);
    temperature_controller_.reset();
    temperature_history_.reset();
    temperature_history_.add(time_ms, telemetry_.actual_temperature_c);
    relay_window_.start(time_ms);
    started_at_ms_ = time_ms;
    last_temperature_at_ms_ = time_ms;
    telemetry_.state = ReflowState::Running;
    telemetry_.elapsed_ms = 0;
    telemetry_.requested_power = HeaterPower::Off;
    telemetry_.active_power = HeaterPower::Off;
    telemetry_.relay_enabled = false;
    ESP_LOGI(TAG, "Reflow started");
}

void ReflowController::on_reflow_aborted(const ReflowAborted&) noexcept {
    lock();
    if (telemetry_.state == ReflowState::Running) {
        stop(ReflowState::Aborted, now_ms());
        ESP_LOGW(TAG, "Reflow aborted by user");
    }
    unlock();
}

void ReflowController::on_temperature_measured(const TemperatureMeasured& message) noexcept {
    const std::uint32_t time_ms = now_ms();
    lock();
    const SafetyFault fault = safety_controller_.check_sample(
        message.temperature_celsius, time_ms, telemetry_.has_temperature,
        telemetry_.actual_temperature_c, last_temperature_at_ms_);
    if (telemetry_.state == ReflowState::Running && fault != SafetyFault::None) {
        stop(ReflowState::Fault, time_ms);
        ESP_LOGE(TAG, "Safety fault: %s", safety_fault_name(fault));
    }
    telemetry_.actual_temperature_c = message.temperature_celsius;
    telemetry_.has_temperature = std::isfinite(message.temperature_celsius);
    last_temperature_at_ms_ = time_ms;
    if (telemetry_.state == ReflowState::Running) {
        update_control(time_ms, message.temperature_celsius);
    }
    unlock();
}

void ReflowController::update_control(const std::uint32_t time_ms,
                                      const float temperature_c) noexcept {
    temperature_history_.add(time_ms, temperature_c);
    const float actual_ramp = temperature_history_.ramp_c_per_s();

    telemetry_.elapsed_ms = time_ms - started_at_ms_;
    const double elapsed_s = static_cast<double>(telemetry_.elapsed_ms) / 1000.0;
    if (elapsed_s >= curve_.back().time_s) {
        stop(ReflowState::Completed, time_ms);
        ESP_LOGI(TAG, "Reflow completed");
        return;
    }
    const TargetPoint target = target_at(curve_, elapsed_s);
    telemetry_.target_temperature_c = target.temperature_c;
    telemetry_.target_ramp_c_per_s = target.ramp_c_per_s;
    telemetry_.actual_ramp_c_per_s = actual_ramp;
    telemetry_.temperature_error_c = target.temperature_c - temperature_c;
    telemetry_.requested_power = temperature_controller_.update(
        {target.temperature_c, temperature_c, target.ramp_c_per_s, actual_ramp,
         static_cast<float>(profile_.max_ramp_rate_c_per_s)});
    relay_window_.request(telemetry_.requested_power);
    apply_relay(relay_window_.update(time_ms));
    telemetry_.active_power = relay_window_.active_power();
    telemetry_.window_progress_ms = relay_window_.window_progress_ms(time_ms);
}

void ReflowController::tick() noexcept {
    const std::uint32_t time_ms = now_ms();
    lock();
    if (telemetry_.state == ReflowState::Running) {
        const SafetyFault fault = safety_controller_.check_staleness(time_ms,
                                                                     last_temperature_at_ms_);
        if (fault != SafetyFault::None) {
            stop(ReflowState::Fault, time_ms);
            ESP_LOGE(TAG, "Safety fault: %s", safety_fault_name(fault));
        } else {
            apply_relay(relay_window_.update(time_ms));
            telemetry_.active_power = relay_window_.active_power();
            telemetry_.window_progress_ms = relay_window_.window_progress_ms(time_ms);
        }
    }
    unlock();
}

void ReflowController::on_temperature_sensor_failed(const TemperatureSensorFailed&) noexcept {
    lock();
    telemetry_.has_temperature = false;
    stop(ReflowState::Fault, now_ms());
    unlock();
}

void ReflowController::on_ota_update_started(const OtaUpdateStarted&) noexcept {
    lock();
    stop(ReflowState::Fault, now_ms());
    unlock();
}

void ReflowController::on_characterization_started(const CharacterizationStarted&) noexcept {
    lock();
    if (telemetry_.state == ReflowState::Running) {
        stop(ReflowState::Aborted, now_ms());
        ESP_LOGW(TAG, "Reflow stopped for oven characterization");
    }
    unlock();
}

void ReflowController::stop(const ReflowState state, const std::uint32_t time_ms) noexcept {
    relay_window_.safety_off(time_ms);
    temperature_controller_.reset();
    telemetry_.state = state;
    telemetry_.requested_power = HeaterPower::Off;
    telemetry_.active_power = HeaterPower::Off;
    telemetry_.relay_enabled = false;
    telemetry_.window_progress_ms = 0;
    bus_.publish(HeaterOutputRequested{false});
}

void ReflowController::apply_relay(const bool enabled) noexcept {
    if (telemetry_.relay_enabled != enabled) {
        telemetry_.relay_enabled = enabled;
        bus_.publish(HeaterOutputRequested{enabled});
    }
}

ControllerTelemetry ReflowController::telemetry() noexcept {
    lock();
    ControllerTelemetry result = telemetry_;
    if (result.state == ReflowState::Running) {
        result.elapsed_ms = now_ms() - started_at_ms_;
    }
    unlock();
    return result;
}

std::uint32_t ReflowController::now_ms() noexcept {
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
}

void ReflowController::lock() noexcept {
    xSemaphoreTake(mutex_, portMAX_DELAY);
}

void ReflowController::unlock() noexcept {
    xSemaphoreGive(mutex_);
}

const char* reflow_state_name(const ReflowState state) noexcept {
    switch (state) {
        case ReflowState::Idle:
            return "idle";
        case ReflowState::Running:
            return "preheat";
        case ReflowState::Completed:
            return "complete";
        case ReflowState::Aborted:
            return "idle";
        case ReflowState::Fault:
            return "error";
    }
    return "error";
}

}  // namespace reflowCtrl
