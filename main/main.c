#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LED_LINK_GPIO GPIO_NUM_23
#define BLINK_PERIOD_MS 500

static const char *TAG = "reflow_pilot";

void app_main(void)
{
    ESP_ERROR_CHECK(gpio_reset_pin(LED_LINK_GPIO));
    ESP_ERROR_CHECK(gpio_set_direction(LED_LINK_GPIO, GPIO_MODE_OUTPUT));

    ESP_LOGI(TAG, "Blinking LedLink on GPIO%d", LED_LINK_GPIO);

    bool led_on = false;

    while (true) {
        led_on = !led_on;
        ESP_ERROR_CHECK(gpio_set_level(LED_LINK_GPIO, led_on));
        vTaskDelay(pdMS_TO_TICKS(BLINK_PERIOD_MS));
    }
}
