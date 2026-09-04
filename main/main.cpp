#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_blinker.hpp"
#include "ota_updater.hpp"
#include "temperature_acquisition.hpp"
#include "web_server.hpp"
#include "wifi_station.hpp"

namespace {

constexpr gpio_num_t LED_LINK_GPIO = GPIO_NUM_23;
constexpr gpio_num_t RELAY_GPIO = GPIO_NUM_16;
constexpr TickType_t UPDATE_PERIOD = pdMS_TO_TICKS(10);
constexpr char TAG[] = "reflowCtrl";

void handle_ota_started(void* context) {
    auto* led_blinker = static_cast<reflowCtrl::LedBlinker*>(context);
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_set_level(RELAY_GPIO, 0));
    led_blinker->request_mode(reflowCtrl::LedBlinkMode::TenHertz);
    ESP_LOGI(TAG, "OTA started; relay disabled and LedLink set to 10 Hz");
}

}  // namespace

extern "C" void app_main() {
    ESP_ERROR_CHECK(gpio_reset_pin(LED_LINK_GPIO));
    ESP_ERROR_CHECK(gpio_set_direction(LED_LINK_GPIO, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_reset_pin(RELAY_GPIO));
    ESP_ERROR_CHECK(gpio_set_direction(RELAY_GPIO, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(RELAY_GPIO, 0));

    static reflowCtrl::LedBlinker led_blinker(reflowCtrl::LedBlinkMode::Slow);

    ESP_LOGI(TAG, "Blinking LedLink on GPIO%d", LED_LINK_GPIO);

    ESP_LOGI(TAG, "Starting Wi-Fi station");
    ESP_ERROR_CHECK(reflowCtrl::start_wifi_station());

    ESP_LOGI(TAG, "Starting OTA updater");
    ESP_ERROR_CHECK(reflowCtrl::start_ota_updater(&handle_ota_started, &led_blinker));

    ESP_LOGI(TAG, "Waiting for Wi-Fi address");
    reflowCtrl::wait_for_wifi_connection();

    static reflowCtrl::TemperatureAcquisition temperature_acquisition;
    ESP_ERROR_CHECK(temperature_acquisition.start());

    ESP_LOGI(TAG, "Starting web server");
    ESP_ERROR_CHECK(reflowCtrl::start_web_server(temperature_acquisition));

    while (true) {
        ESP_ERROR_CHECK(gpio_set_level(LED_LINK_GPIO, led_blinker.is_on()));
        vTaskDelay(UPDATE_PERIOD);
        led_blinker.advance(10);
    }
}
