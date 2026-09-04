#include "button_debouncer.hpp"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_blinker.hpp"
#include "message_bus.hpp"
#include "ota_updater.hpp"
#include "temperature_acquisition.hpp"
#include "web_server.hpp"
#include "wifi_station.hpp"

namespace {

constexpr gpio_num_t LED_LINK_GPIO = GPIO_NUM_23;
constexpr gpio_num_t RELAY_GPIO = GPIO_NUM_16;
constexpr gpio_num_t BUTTON_GPIO = GPIO_NUM_0;
constexpr TickType_t UPDATE_PERIOD = pdMS_TO_TICKS(10);
constexpr std::uint32_t BUTTON_DEBOUNCE_SAMPLE_COUNT = 4;
constexpr char TAG[] = "reflowCtrl";

class StatusIndicator {
   public:
    explicit StatusIndicator(reflowCtrl::MessageBus& bus) noexcept : bus_(bus) {}
    esp_err_t start();
    void tick() noexcept;
    void on_ota_started(const reflowCtrl::OtaUpdateStarted&) noexcept;

   private:
    reflowCtrl::MessageBus& bus_;
    reflowCtrl::LedBlinker blinker_{reflowCtrl::LedBlinkMode::Slow};
};

class SafetyOutputs {
   public:
    explicit SafetyOutputs(reflowCtrl::MessageBus& bus) noexcept : bus_(bus) {}
    esp_err_t start();
    void on_ota_started(const reflowCtrl::OtaUpdateStarted&) noexcept;

   private:
    reflowCtrl::MessageBus& bus_;
};

class ButtonInput {
   public:
    explicit ButtonInput(reflowCtrl::MessageBus& bus) noexcept : bus_(bus) {}
    esp_err_t start();
    void tick() noexcept;

   private:
    reflowCtrl::MessageBus& bus_;
    reflowCtrl::ButtonDebouncer debouncer_{BUTTON_DEBOUNCE_SAMPLE_COUNT};
};

class ButtonPressLogger {
   public:
    explicit ButtonPressLogger(reflowCtrl::MessageBus& bus) noexcept : bus_(bus) {}
    esp_err_t start();
    void on_button_pressed(const reflowCtrl::ButtonPressed& message) noexcept;

   private:
    reflowCtrl::MessageBus& bus_;
};

esp_err_t StatusIndicator::start() {
    ESP_RETURN_ON_ERROR(gpio_reset_pin(LED_LINK_GPIO), TAG, "Could not reset LED pin");
    ESP_RETURN_ON_ERROR(gpio_set_direction(LED_LINK_GPIO, GPIO_MODE_OUTPUT), TAG,
                        "Could not configure LED pin");
    return bus_.subscribe<reflowCtrl::OtaUpdateStarted>(&StatusIndicator::on_ota_started, this)
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}

void StatusIndicator::tick() noexcept {
    gpio_set_level(LED_LINK_GPIO, blinker_.is_on());
    blinker_.advance(10);
}

void StatusIndicator::on_ota_started(const reflowCtrl::OtaUpdateStarted&) noexcept {
    blinker_.request_mode(reflowCtrl::LedBlinkMode::TenHertz);
}

esp_err_t SafetyOutputs::start() {
    ESP_RETURN_ON_ERROR(gpio_reset_pin(RELAY_GPIO), TAG, "Could not reset relay pin");
    ESP_RETURN_ON_ERROR(gpio_set_direction(RELAY_GPIO, GPIO_MODE_OUTPUT), TAG,
                        "Could not configure relay pin");
    ESP_RETURN_ON_ERROR(gpio_set_level(RELAY_GPIO, 0), TAG, "Could not disable relay");
    return bus_.subscribe<reflowCtrl::OtaUpdateStarted>(&SafetyOutputs::on_ota_started, this)
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}

void SafetyOutputs::on_ota_started(const reflowCtrl::OtaUpdateStarted&) noexcept {
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_set_level(RELAY_GPIO, 0));
}

esp_err_t ButtonInput::start() {
    ESP_RETURN_ON_ERROR(gpio_reset_pin(BUTTON_GPIO), TAG, "Could not reset button pin");
    ESP_RETURN_ON_ERROR(gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT), TAG,
                        "Could not configure button pin");
    return gpio_set_pull_mode(BUTTON_GPIO, GPIO_PULLUP_ONLY);
}

void ButtonInput::tick() noexcept {
    if (debouncer_.update(gpio_get_level(BUTTON_GPIO) == 0)) {
        bus_.publish(reflowCtrl::ButtonPressed{0});
    }
}

esp_err_t ButtonPressLogger::start() {
    return bus_.subscribe<reflowCtrl::ButtonPressed>(&ButtonPressLogger::on_button_pressed, this)
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}

void ButtonPressLogger::on_button_pressed(const reflowCtrl::ButtonPressed& message) noexcept {
    ESP_LOGI(TAG, "Button %u pressed", static_cast<unsigned>(message.button));
}

}  // namespace

extern "C" void app_main() {
    static reflowCtrl::MessageBus bus;
    static StatusIndicator status_indicator(bus);
    static SafetyOutputs safety_outputs(bus);
    static ButtonInput button(bus);
    static ButtonPressLogger button_logger(bus);
    static reflowCtrl::OtaUpdater ota_updater(bus);
    static reflowCtrl::TemperatureAcquisition temperature_acquisition(bus);
    static reflowCtrl::WebServer web_server(bus);

    ESP_ERROR_CHECK(status_indicator.start());
    ESP_ERROR_CHECK(safety_outputs.start());
    ESP_ERROR_CHECK(button.start());
    ESP_ERROR_CHECK(button_logger.start());
    ESP_LOGI(TAG, "Button on GPIO%d (active low, 40 ms debounce)", BUTTON_GPIO);
    ESP_LOGI(TAG, "Starting Wi-Fi station");
    ESP_ERROR_CHECK(reflowCtrl::start_wifi_station());
    ESP_LOGI(TAG, "Starting OTA updater");
    ESP_ERROR_CHECK(ota_updater.start());
    ESP_LOGI(TAG, "Waiting for Wi-Fi address");
    reflowCtrl::wait_for_wifi_connection();
    ESP_ERROR_CHECK(temperature_acquisition.start());
    ESP_LOGI(TAG, "Starting web server");
    ESP_ERROR_CHECK(web_server.start());
    bus.publish(reflowCtrl::ControllerStarted{});

    while (true) {
        status_indicator.tick();
        button.tick();
        vTaskDelay(UPDATE_PERIOD);
    }
}
