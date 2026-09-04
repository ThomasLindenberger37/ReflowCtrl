#ifndef REFLOWCTRL_MESSAGE_BUS_HPP
#define REFLOWCTRL_MESSAGE_BUS_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <variant>

#include "messages.hpp"

namespace reflowCtrl {
namespace detail {

template <typename Message, typename... RegisteredMessages>
consteval bool is_registered_message(const std::variant<RegisteredMessages...>*) {
    return (std::is_same_v<Message, RegisteredMessages> || ...);
}

}  // namespace detail

class MessageBus {
   public:
    static constexpr std::size_t MAX_SUBSCRIPTIONS = 40;

    MessageBus() = default;
    MessageBus(const MessageBus&) = delete;
    MessageBus& operator=(const MessageBus&) = delete;

    template <typename Message, typename Component>
    [[nodiscard]] bool subscribe(void (Component::*handler)(const Message&),
                                 Component* const component) noexcept {
        static_assert(is_registered<Message>(), "Message must be registered in MessageTypes");
        static_assert(sizeof(handler) <= MAX_MEMBER_HANDLER_SIZE,
                      "Member handler is too large for MessageBus storage");

        if (subscription_count_ == subscriptions_.size() || component == nullptr) {
            return false;
        }

        Subscription& subscription = subscriptions_[subscription_count_++];
        subscription.context = component;
        subscription.message_index = message_index<Message>();
        subscription.dispatch = &dispatch_member<Message, Component>;
        std::memcpy(subscription.handler.data(), &handler, sizeof(handler));
        return true;
    }

    template <typename Message>
    void publish(const Message& message) const noexcept {
        static_assert(is_registered<Message>(), "Message must be registered in MessageTypes");
        const MessageTypes typed_message{std::in_place_type<Message>, message};
        publish_message(message_index<Message>(), typed_message);
    }

   private:
    static constexpr std::size_t MAX_MEMBER_HANDLER_SIZE = 16;

    struct Subscription;
    using Dispatch = void (*)(const Subscription&, const MessageTypes&);

    struct Subscription {
        void* context = nullptr;
        std::array<std::byte, MAX_MEMBER_HANDLER_SIZE> handler{};
        Dispatch dispatch = nullptr;
        std::size_t message_index = 0;
    };

    template <typename Message>
    static consteval bool is_registered() {
        return detail::is_registered_message<Message>(static_cast<const MessageTypes*>(nullptr));
    }

    template <typename Message>
    static consteval std::size_t message_index() {
        return MessageTypes{std::in_place_type<Message>}.index();
    }

    template <typename Message, typename Component>
    static void dispatch_member(const Subscription& subscription,
                                const MessageTypes& message) noexcept {
        using Handler = void (Component::*)(const Message&);
        Handler handler = nullptr;
        std::memcpy(&handler, subscription.handler.data(), sizeof(handler));
        auto* const component = static_cast<Component*>(subscription.context);
        (component->*handler)(std::get<Message>(message));
    }

    void publish_message(std::size_t message_index, const MessageTypes& message) const noexcept;

    std::array<Subscription, MAX_SUBSCRIPTIONS> subscriptions_{};
    std::size_t subscription_count_ = 0;
};

}  // namespace reflowCtrl

#endif  // REFLOWCTRL_MESSAGE_BUS_HPP
