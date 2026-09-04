#ifndef REFLOWCTRL_BUTTON_PRESS_LOGGER_HPP
#define REFLOWCTRL_BUTTON_PRESS_LOGGER_HPP

#include "esp_err.h"
#include "message_bus.hpp"

namespace reflowCtrl {

class ButtonPressLogger {
   public:
    explicit ButtonPressLogger(MessageBus& bus) noexcept : bus_(bus) {}

    esp_err_t start();
    void on_button_pressed(const ButtonPressed& message) noexcept;

   private:
    MessageBus& bus_;
};

}  // namespace reflowCtrl

#endif  // REFLOWCTRL_BUTTON_PRESS_LOGGER_HPP
