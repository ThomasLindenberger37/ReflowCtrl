#ifndef REFLOWCTRL_REFLOW_CONTROLLER_HPP
#define REFLOWCTRL_REFLOW_CONTROLLER_HPP

#include <cstdint>
#include <optional>
#include <vector>

#include "components/relay_window_controller.hpp"
#include "components/safety_controller.hpp"
#include "components/temperature_history.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "message_bus.hpp"
#include "profile.hpp"

namespace reflowCtrl {

enum class ReflowState : std::uint8_t { Idle, Running, Completed, Aborted, Fault };

struct ControllerTelemetry {
    ReflowState state = ReflowState::Idle;
    float target_temperature_c = 0.0F;
    float actual_temperature_c = 0.0F;
    float temperature_error_c = 0.0F;
    float target_ramp_c_per_s = 0.0F;
    float actual_ramp_c_per_s = 0.0F;
    HeaterPower requested_power = HeaterPower::Off;
    HeaterPower active_power = HeaterPower::Off;
    bool relay_enabled = false;
    bool has_temperature = false;
    std::uint32_t elapsed_ms = 0;
    std::uint32_t window_progress_ms = 0;
};

class ReflowController {
   public:
    explicit ReflowController(MessageBus& bus) noexcept : bus_(bus) {}

    esp_err_t start() noexcept;
    void tick() noexcept;
    void on_reflow_started(const ReflowStarted&) noexcept;
    void on_reflow_aborted(const ReflowAborted&) noexcept;
    void on_temperature_measured(const TemperatureMeasured& message) noexcept;
    void on_temperature_sensor_failed(const TemperatureSensorFailed&) noexcept;
    void on_ota_update_started(const OtaUpdateStarted&) noexcept;
    void on_characterization_started(const CharacterizationStarted&) noexcept;
    [[nodiscard]] ControllerTelemetry telemetry() noexcept;

   private:
    [[nodiscard]] static std::uint32_t now_ms() noexcept;
    void start_process(std::uint32_t now_ms) noexcept;
    void update_control(std::uint32_t now_ms, float temperature_c) noexcept;
    void stop(ReflowState state, std::uint32_t now_ms) noexcept;
    void apply_relay(bool enabled) noexcept;
    void lock() noexcept;
    void unlock() noexcept;

    MessageBus& bus_;
    TemperatureController temperature_controller_;
    RelayWindowController relay_window_;
    SafetyController safety_controller_;
    TemperatureHistory temperature_history_;
    std::vector<CurvePoint> curve_;
    ProfileConfiguration profile_;
    std::optional<OvenCapabilities> oven_;
    ControllerTelemetry telemetry_;
    StaticSemaphore_t mutex_storage_{};
    SemaphoreHandle_t mutex_ = nullptr;
    std::uint32_t started_at_ms_ = 0;
    std::uint32_t last_temperature_at_ms_ = 0;
};

[[nodiscard]] const char* reflow_state_name(ReflowState state) noexcept;

}  // namespace reflowCtrl

#endif  // REFLOWCTRL_REFLOW_CONTROLLER_HPP
