#include "components/temperature_controller.hpp"

#include <cmath>

#include "components/relay_window_controller.hpp"
#include "components/safety_controller.hpp"
#include "components/temperature_history.hpp"
#include "gtest/gtest.h"

namespace reflowCtrl {
namespace {

TEST(RelayWindowController, ProducesExpectedOnTimeForEveryPowerLevel) {
    const struct {
        HeaterPower power;
        std::uint32_t on_time_ms;
    } cases[] = {{HeaterPower::Off, 0},
                 {HeaterPower::P25, 1'000},
                 {HeaterPower::P50, 2'000},
                 {HeaterPower::P75, 3'000},
                 {HeaterPower::P100, 4'000}};
    for (const auto& test : cases) {
        RelayWindowController relay;
        relay.start(0);
        relay.request(test.power);
        EXPECT_EQ(relay.update(4'000), test.on_time_ms > 0);
        if (test.on_time_ms < RelayWindowController::WINDOW_MS) {
            EXPECT_FALSE(relay.update(4'000 + test.on_time_ms));
        } else {
            EXPECT_TRUE(relay.update(7'999));
        }
    }
}

TEST(RelayWindowController, AppliesPowerOnlyAtNextWindow) {
    RelayWindowController relay;
    relay.start(100);
    relay.request(HeaterPower::P25);
    EXPECT_FALSE(relay.update(3'999));
    EXPECT_TRUE(relay.update(4'100));
    relay.request(HeaterPower::P100);
    EXPECT_FALSE(relay.update(5'100));
    EXPECT_TRUE(relay.update(8'100));
}

TEST(RelayWindowController, SafetyOffIsImmediate) {
    RelayWindowController relay;
    relay.start(0);
    relay.request(HeaterPower::P100);
    ASSERT_TRUE(relay.update(4'000));
    relay.safety_off(4'500);
    EXPECT_FALSE(relay.relay_enabled());
    EXPECT_EQ(relay.active_power(), HeaterPower::Off);
    EXPECT_EQ(relay.requested_power(), HeaterPower::Off);
}

TEST(RelayWindowController, SwitchesAtMostOnThenOffWithinAWindow) {
    RelayWindowController relay;
    relay.start(0);
    relay.request(HeaterPower::P75);
    bool previous = relay.update(4'000);
    int transitions = 0;
    for (std::uint32_t time_ms = 4'100; time_ms < 8'000; time_ms += 100) {
        const bool current = relay.update(time_ms);
        transitions += current != previous ? 1 : 0;
        previous = current;
    }
    EXPECT_EQ(transitions, 1);
}

TEST(TemperatureController, QuantizesCharacterizationFeedforward) {
    TemperatureController controller;
    const TemperatureControlInput input{150.0F, 147.0F, 1.0F, 1.0F, 2.0F};
    EXPECT_EQ(controller.update(input), HeaterPower::P75);
}

TEST(TemperatureController, CoolingAlwaysTurnsHeatingOff) {
    TemperatureController controller;
    EXPECT_NE(controller.update({150.0F, 130.0F, 1.0F, 0.5F, 2.0F}), HeaterPower::Off);
    EXPECT_EQ(controller.update({149.0F, 130.0F, -0.1F, 0.5F, 2.0F}), HeaterPower::Off);
}

TEST(TemperatureController, HysteresisHoldsCurrentPower) {
    TemperatureController controller;
    const HeaterPower initial = controller.update({150.0F, 140.0F, 1.0F, 0.5F, 2.0F});
    EXPECT_EQ(controller.update({150.0F, 149.0F, 1.0F, 0.5F, 2.0F}), initial);
}

TEST(TemperatureController, OvershootTurnsHeatingOff) {
    TemperatureController controller;
    EXPECT_NE(controller.update({150.0F, 140.0F, 1.0F, 0.5F, 2.0F}), HeaterPower::Off);
    EXPECT_EQ(controller.update({150.0F, 153.0F, 0.0F, 1.0F, 2.0F}), HeaterPower::Off);
}

TEST(TemperatureController, TemperatureDeficitRaisesPower) {
    TemperatureController controller;
    EXPECT_EQ(controller.update({150.0F, 140.0F, 0.5F, 0.5F, 2.0F}), HeaterPower::P75);
}

TEST(TemperatureController, ExcessActualRampReducesPowerBeforeTarget) {
    TemperatureController normal;
    TemperatureController anticipating;
    const HeaterPower normal_power = normal.update({180.0F, 175.0F, 1.0F, 1.0F, 2.0F});
    const HeaterPower reduced_power = anticipating.update({180.0F, 175.0F, 1.0F, 2.5F, 2.0F});
    EXPECT_LT(heater_power_percent(reduced_power), heater_power_percent(normal_power));
}

TEST(TemperatureHistory, UsesSmoothedHistoryForRamp) {
    TemperatureHistory history;
    history.add(0, 100.0F);
    history.add(200, 100.3F);
    history.add(400, 100.8F);
    history.add(600, 101.2F);
    EXPECT_NEAR(history.ramp_c_per_s(), 2.0F, 0.001F);
}

TEST(SafetyController, RejectsInvalidOvertemperatureAndImplausibleSamples) {
    SafetyController safety;
    EXPECT_EQ(safety.check_sample(NAN, 1'000, false, 0.0F, 0),
              SafetyFault::InvalidTemperature);
    EXPECT_EQ(safety.check_sample(250.0F, 1'000, false, 0.0F, 0),
              SafetyFault::Overtemperature);
    EXPECT_EQ(safety.check_sample(130.1F, 1'000, true, 100.0F, 0),
              SafetyFault::ImplausibleTemperatureChange);
    EXPECT_EQ(safety.check_sample(130.0F, 1'000, true, 100.0F, 0), SafetyFault::None);
}

TEST(SafetyController, DetectsStaleMeasurementsAfterTimeout) {
    SafetyController safety;
    EXPECT_EQ(safety.check_staleness(3'000, 1'000), SafetyFault::None);
    EXPECT_EQ(safety.check_staleness(3'001, 1'000), SafetyFault::StaleTemperature);
}

}  // namespace
}  // namespace reflowCtrl
