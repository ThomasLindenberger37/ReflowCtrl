#include "message_bus.hpp"

namespace reflowCtrl {

void MessageBus::publish_message(const std::size_t message_index,
                                 const MessageTypes& message) const noexcept {
    for (std::size_t index = 0; index < subscription_count_; ++index) {
        const Subscription& subscription = subscriptions_[index];
        if (subscription.message_index == message_index) {
            subscription.dispatch(subscription, message);
        }
    }
}

}  // namespace reflowCtrl
