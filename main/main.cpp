#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "led_blinker.hpp"

namespace {

constexpr gpio_num_t LED_LINK_GPIO = GPIO_NUM_23;
constexpr TickType_t UPDATE_PERIOD = pdMS_TO_TICKS(10);
constexpr char TAG[] = "reflow_pilot";

}  // namespace

extern "C" void app_main()
{
    ESP_ERROR_CHECK(gpio_reset_pin(LED_LINK_GPIO));
    ESP_ERROR_CHECK(gpio_set_direction(LED_LINK_GPIO, GPIO_MODE_OUTPUT));

    ESP_LOGI(TAG, "Blinking LedLink on GPIO%d", LED_LINK_GPIO);

    reflow_pilot::LedBlinker led_blinker(reflow_pilot::LedBlinkMode::Slow);

    while (true) {
        ESP_ERROR_CHECK(gpio_set_level(LED_LINK_GPIO, led_blinker.is_on()));
        vTaskDelay(UPDATE_PERIOD);
        led_blinker.advance(10);
    }
}
