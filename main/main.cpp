#include "components/button.hpp"
#include "components/button_press_logger.hpp"
#include "components/characterization_controller.hpp"
#include "components/characterization_live_feed.hpp"
#include "components/max6675.hpp"
#include "components/reflow_controller.hpp"
#include "components/relay_output.hpp"
#include "components/status_led.hpp"
#include "components/temperature_acquisition.hpp"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hardware/gpio_pins.hpp"
#include "hardware/hardware_configuration.hpp"
#include "message_bus.hpp"
#include "ota_updater.hpp"
#include "web_server.hpp"
#include "wifi_station.hpp"

namespace {

constexpr TickType_t UPDATE_PERIOD = pdMS_TO_TICKS(10);
constexpr std::uint32_t DIAGNOSTIC_INTERVAL_MS = 10'000;
constexpr std::uint32_t BUTTON_DEBOUNCE_SAMPLE_COUNT = 4;
constexpr char TAG[] = "reflowCtrl";

}  // namespace

extern "C" void app_main() {
    const reflowCtrl::HardwareConfiguration& hardware = reflowCtrl::hardware_configuration();

    static reflowCtrl::MessageBus bus;
    static reflowCtrl::GpioOutput status_led_pin(hardware.status_led_pin);
    static reflowCtrl::GpioOutput relay_pin(hardware.relay_pin);
    static reflowCtrl::GpioInput button_pin(hardware.button_pin, true);
    static reflowCtrl::GpioInput max6675_so_pin(hardware.max6675_so_pin, false);
    static reflowCtrl::GpioOutput max6675_sck_pin(hardware.max6675_sck_pin);
    static reflowCtrl::GpioOutput max6675_cs_pin(hardware.max6675_cs_pin);
    static reflowCtrl::StatusLed status_led(bus, status_led_pin);
    static reflowCtrl::RelayOutput relay(bus, relay_pin);
    static reflowCtrl::Button button(bus, button_pin, 0, BUTTON_DEBOUNCE_SAMPLE_COUNT);
    static reflowCtrl::ButtonPressLogger button_logger(bus);
    static reflowCtrl::CharacterizationLiveFeed characterization_live_feed;
    static reflowCtrl::CharacterizationController characterization(bus, characterization_live_feed);
    static reflowCtrl::ReflowController reflow(bus);
    static reflowCtrl::Max6675 max6675(max6675_so_pin, max6675_sck_pin, max6675_cs_pin);
    static reflowCtrl::TemperatureAcquisition temperature_acquisition(bus, max6675);
    static reflowCtrl::OtaUpdater ota_updater(bus);
    static reflowCtrl::WebServer web_server(bus, characterization, reflow);

    ESP_ERROR_CHECK(status_led_pin.initialize(false));
    ESP_ERROR_CHECK(relay_pin.initialize(false));
    ESP_ERROR_CHECK(button_pin.initialize(GPIO_PULLUP_ONLY));
    ESP_ERROR_CHECK(max6675_so_pin.initialize(GPIO_FLOATING));
    ESP_ERROR_CHECK(max6675_sck_pin.initialize(true));
    ESP_ERROR_CHECK(max6675_cs_pin.initialize(true));
    ESP_ERROR_CHECK(status_led.start());
    ESP_ERROR_CHECK(relay.start());
    ESP_ERROR_CHECK(button_logger.start());
    ESP_ERROR_CHECK(characterization.start());
    ESP_ERROR_CHECK(reflow.start());
    ESP_LOGI(TAG, "Hardware configuration applied");

    ESP_ERROR_CHECK(reflowCtrl::start_wifi_station(bus));
    ESP_ERROR_CHECK(ota_updater.start());
    reflowCtrl::wait_for_wifi_connection();
    ESP_ERROR_CHECK(temperature_acquisition.start());
    ESP_ERROR_CHECK(web_server.start());
    bus.publish(reflowCtrl::ControllerStarted{});
    std::uint32_t last_diagnostic_ms = 0;

    while (true) {
        status_led.tick(10);
        button.tick();
        reflow.tick();
        const std::uint32_t now_ms = static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
        if (now_ms - last_diagnostic_ms >= DIAGNOSTIC_INTERVAL_MS) {
            last_diagnostic_ms = now_ms;
            const reflowCtrl::ControllerTelemetry telemetry = reflow.telemetry();
            if (telemetry.state == reflowCtrl::ReflowState::Running) {
                ESP_LOGI(TAG,
                         "Run health: elapsed=%lu s actual=%.1f C target=%.1f C relay=%s "
                         "requested=%u%% active=%u%% heap=%lu min_heap=%lu main_stack=%u",
                         static_cast<unsigned long>(telemetry.elapsed_ms / 1000),
                         static_cast<double>(telemetry.actual_temperature_c),
                         static_cast<double>(telemetry.target_temperature_c),
                         telemetry.relay_enabled ? "on" : "off",
                         reflowCtrl::heater_power_percent(telemetry.requested_power),
                         reflowCtrl::heater_power_percent(telemetry.active_power),
                         static_cast<unsigned long>(esp_get_free_heap_size()),
                         static_cast<unsigned long>(esp_get_minimum_free_heap_size()),
                         static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
            }
        }
        vTaskDelay(UPDATE_PERIOD);
    }
}
