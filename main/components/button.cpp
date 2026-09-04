#include "components/button.hpp"

namespace reflowCtrl {

void Button::tick() noexcept {
    if (debouncer_.update(input_.is_active())) {
        bus_.publish(ButtonPressed{identifier_});
    }
}

}  // namespace reflowCtrl
