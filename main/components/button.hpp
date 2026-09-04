#ifndef REFLOWCTRL_BUTTON_HPP
#define REFLOWCTRL_BUTTON_HPP

#include "components/button_debouncer.hpp"
#include "components/digital_io.hpp"
#include "message_bus.hpp"

namespace reflowCtrl {

class Button {
   public:
    Button(MessageBus& bus, DigitalInput& input, std::uint8_t identifier,
           std::uint32_t stable_sample_count) noexcept
        : bus_(bus), input_(input), identifier_(identifier), debouncer_(stable_sample_count) {}

    void tick() noexcept;

   private:
    MessageBus& bus_;
    DigitalInput& input_;
    std::uint8_t identifier_;
    ButtonDebouncer debouncer_;
};

}  // namespace reflowCtrl

#endif  // REFLOWCTRL_BUTTON_HPP
