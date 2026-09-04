#include "components/button_press_logger.hpp"

#include "esp_log.h"

namespace reflowCtrl {
namespace {

constexpr char TAG[] = "button";

}  // namespace

esp_err_t ButtonPressLogger::start() {
    return bus_.subscribe<ButtonPressed>(&ButtonPressLogger::on_button_pressed, this)
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}

void ButtonPressLogger::on_button_pressed(const ButtonPressed& message) noexcept {
    ESP_LOGI(TAG, "Button %u pressed", static_cast<unsigned>(message.button));
}

}  // namespace reflowCtrl
