#include "components/button.hpp"
#include "components/button_press_logger.hpp"
#include "components/max6675.hpp"
#include "components/relay_output.hpp"
#include "components/status_led.hpp"
#include "components/temperature_acquisition.hpp"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
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
    static reflowCtrl::Max6675 max6675(max6675_so_pin, max6675_sck_pin, max6675_cs_pin);
    static reflowCtrl::TemperatureAcquisition temperature_acquisition(bus, max6675);
    static reflowCtrl::OtaUpdater ota_updater(bus);
    static reflowCtrl::WebServer web_server(bus);

    ESP_ERROR_CHECK(status_led_pin.initialize(false));
    ESP_ERROR_CHECK(relay_pin.initialize(false));
    ESP_ERROR_CHECK(button_pin.initialize(GPIO_PULLUP_ONLY));
    ESP_ERROR_CHECK(max6675_so_pin.initialize(GPIO_FLOATING));
    ESP_ERROR_CHECK(max6675_sck_pin.initialize(true));
    ESP_ERROR_CHECK(max6675_cs_pin.initialize(true));
    ESP_ERROR_CHECK(status_led.start());
    ESP_ERROR_CHECK(relay.start());
    ESP_ERROR_CHECK(button_logger.start());
    ESP_LOGI(TAG, "Hardware configuration applied");

    ESP_ERROR_CHECK(reflowCtrl::start_wifi_station(bus));
    ESP_ERROR_CHECK(ota_updater.start());
    reflowCtrl::wait_for_wifi_connection();
    ESP_ERROR_CHECK(temperature_acquisition.start());
    ESP_ERROR_CHECK(web_server.start());
    bus.publish(reflowCtrl::ControllerStarted{});

    while (true) {
        status_led.tick(10);
        button.tick();
        vTaskDelay(UPDATE_PERIOD);
    }
}
