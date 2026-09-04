#include "message_bus.hpp"

#include "gtest/gtest.h"

namespace reflowCtrl {
namespace {

class TemperatureSubscriber {
   public:
    void on_temperature(const TemperatureMeasured& message) noexcept {
        temperature_celsius = message.temperature_celsius;
        call_count++;
    }

    float temperature_celsius = 0.0F;
    int call_count = 0;
};

class ButtonSubscriber {
   public:
    void on_button(const ButtonPressed& message) noexcept {
        button = message.button;
        call_count++;
    }

    std::uint8_t button = 0;
    int call_count = 0;
};

class MultiMessageSubscriber {
   public:
    void on_temperature(const TemperatureMeasured&) noexcept {
        temperature_count++;
    }

    void on_button(const ButtonPressed&) noexcept {
        button_count++;
    }

    int temperature_count = 0;
    int button_count = 0;
};

TEST(MessageBusTest, AllowsPublishingWithoutSubscribers) {
    MessageBus bus;

    bus.publish(ControllerStarted{});
}

TEST(MessageBusTest, DeliversMessagesToMatchingTypedSubscribers) {
    MessageBus bus;
    TemperatureSubscriber temperature_subscriber;
    ButtonSubscriber button_subscriber;

    ASSERT_TRUE(bus.subscribe<TemperatureMeasured>(&TemperatureSubscriber::on_temperature,
                                                   &temperature_subscriber));
    ASSERT_TRUE(bus.subscribe<ButtonPressed>(&ButtonSubscriber::on_button, &button_subscriber));

    bus.publish(TemperatureMeasured{217.5F});

    EXPECT_EQ(temperature_subscriber.call_count, 1);
    EXPECT_FLOAT_EQ(temperature_subscriber.temperature_celsius, 217.5F);
    EXPECT_EQ(button_subscriber.call_count, 0);
}

TEST(MessageBusTest, ExecutesSubscribersSynchronouslyInSubscriptionOrder) {
    MessageBus bus;
    TemperatureSubscriber first;
    TemperatureSubscriber second;

    ASSERT_TRUE(bus.subscribe<TemperatureMeasured>(&TemperatureSubscriber::on_temperature, &first));
    ASSERT_TRUE(
        bus.subscribe<TemperatureMeasured>(&TemperatureSubscriber::on_temperature, &second));

    bus.publish(TemperatureMeasured{42.0F});

    EXPECT_EQ(first.call_count, 1);
    EXPECT_EQ(second.call_count, 1);
}

TEST(MessageBusTest, DeliversDifferentMessagesToOneComponent) {
    MessageBus bus;
    MultiMessageSubscriber subscriber;

    ASSERT_TRUE(
        bus.subscribe<TemperatureMeasured>(&MultiMessageSubscriber::on_temperature, &subscriber));
    ASSERT_TRUE(bus.subscribe<ButtonPressed>(&MultiMessageSubscriber::on_button, &subscriber));

    bus.publish(TemperatureMeasured{120.0F});
    bus.publish(ButtonPressed{2});

    EXPECT_EQ(subscriber.temperature_count, 1);
    EXPECT_EQ(subscriber.button_count, 1);
}

TEST(MessageBusTest, RejectsTheEleventhSubscription) {
    MessageBus bus;
    std::array<TemperatureSubscriber, MessageBus::MAX_SUBSCRIPTIONS + 1> subscribers{};

    for (std::size_t index = 0; index < MessageBus::MAX_SUBSCRIPTIONS; ++index) {
        EXPECT_TRUE(bus.subscribe<TemperatureMeasured>(&TemperatureSubscriber::on_temperature,
                                                       &subscribers[index]));
    }
    EXPECT_FALSE(bus.subscribe<TemperatureMeasured>(&TemperatureSubscriber::on_temperature,
                                                    &subscribers.back()));
}

}  // namespace
}  // namespace reflowCtrl
