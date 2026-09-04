#include "led_blinker.hpp"

#include "gtest/gtest.h"

namespace reflowCtrl {
namespace {

TEST(LedBlinkerTest, TenHertzModeCompletesOneCycleEvery100Milliseconds) {
    LedBlinker blinker(LedBlinkMode::TenHertz);

    EXPECT_TRUE(blinker.is_on());
    blinker.advance(49);
    EXPECT_TRUE(blinker.is_on());
    blinker.advance(1);
    EXPECT_FALSE(blinker.is_on());
    blinker.advance(50);
    EXPECT_TRUE(blinker.is_on());
}

TEST(LedBlinkerTest, FastModeAlternatesEvery200Milliseconds) {
    LedBlinker blinker(LedBlinkMode::Fast);

    EXPECT_TRUE(blinker.is_on());
    blinker.advance(199);
    EXPECT_TRUE(blinker.is_on());
    blinker.advance(1);
    EXPECT_FALSE(blinker.is_on());
    blinker.advance(200);
    EXPECT_TRUE(blinker.is_on());
}

TEST(LedBlinkerTest, SlowModeAlternatesEverySecond) {
    LedBlinker blinker(LedBlinkMode::Slow);

    blinker.advance(999);
    EXPECT_TRUE(blinker.is_on());
    blinker.advance(1);
    EXPECT_FALSE(blinker.is_on());
}

TEST(LedBlinkerTest, LongOnShortOffModeUsesExpectedPhases) {
    LedBlinker blinker(LedBlinkMode::LongOnShortOff);

    EXPECT_TRUE(blinker.is_on());
    blinker.advance(1000);
    EXPECT_FALSE(blinker.is_on());
    blinker.advance(200);
    EXPECT_TRUE(blinker.is_on());
}

TEST(LedBlinkerTest, LongOffShortOnModeUsesExpectedPhases) {
    LedBlinker blinker(LedBlinkMode::LongOffShortOn);

    EXPECT_FALSE(blinker.is_on());
    blinker.advance(1000);
    EXPECT_TRUE(blinker.is_on());
    blinker.advance(200);
    EXPECT_FALSE(blinker.is_on());
}

TEST(LedBlinkerTest, ConstantModesNeverChangeState) {
    LedBlinker on_blinker(LedBlinkMode::On);
    LedBlinker off_blinker(LedBlinkMode::Off);

    on_blinker.advance(10'000);
    off_blinker.advance(10'000);

    EXPECT_TRUE(on_blinker.is_on());
    EXPECT_FALSE(off_blinker.is_on());
}

TEST(LedBlinkerTest, ChangingModeResetsThePhase) {
    LedBlinker blinker(LedBlinkMode::Fast);

    blinker.advance(150);
    blinker.set_mode(LedBlinkMode::LongOffShortOn);

    EXPECT_EQ(blinker.mode(), LedBlinkMode::LongOffShortOn);
    EXPECT_FALSE(blinker.is_on());
    blinker.advance(999);
    EXPECT_FALSE(blinker.is_on());
    blinker.advance(1);
    EXPECT_TRUE(blinker.is_on());
}

TEST(LedBlinkerTest, AppliesRequestedModeOnNextAdvance) {
    LedBlinker blinker(LedBlinkMode::Slow);

    blinker.request_mode(LedBlinkMode::TenHertz);
    EXPECT_EQ(blinker.mode(), LedBlinkMode::Slow);

    blinker.advance(0);
    EXPECT_EQ(blinker.mode(), LedBlinkMode::TenHertz);
    EXPECT_TRUE(blinker.is_on());
    blinker.advance(50);
    EXPECT_FALSE(blinker.is_on());
}

}  // namespace
}  // namespace reflowCtrl
