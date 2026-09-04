#include "components/characterization_sequence.hpp"

#include "gtest/gtest.h"

namespace reflowCtrl {
namespace {

TEST(CharacterizationSequenceTest, TransitionsFromBaselineToFirstHeatAfterThirtySeconds) {
    CharacterizationSequence sequence;

    sequence.start(100);
    sequence.update(30'099, 25.0F);
    EXPECT_EQ(sequence.phase(), CharacterizationPhase::baseline);
    sequence.update(30'100, 25.0F);

    EXPECT_EQ(sequence.phase(), CharacterizationPhase::heat_1);
    EXPECT_TRUE(sequence.heater_enabled());
}

TEST(CharacterizationSequenceTest, TransitionsEachHeatAndCoastPhase) {
    CharacterizationSequence sequence;
    sequence.start(0);
    sequence.update(30'000, 25.0F);
    sequence.update(30'200, 80.0F);
    EXPECT_EQ(sequence.phase(), CharacterizationPhase::coast_1);
    EXPECT_FALSE(sequence.heater_enabled());

    sequence.update(50'200, 85.0F);
    EXPECT_EQ(sequence.phase(), CharacterizationPhase::heat_2);
    EXPECT_TRUE(sequence.heater_enabled());

    sequence.update(50'400, 130.0F);
    sequence.update(70'400, 135.0F);
    EXPECT_EQ(sequence.phase(), CharacterizationPhase::heat_3);

    sequence.update(70'600, 180.0F);
    sequence.update(90'600, 185.0F);
    EXPECT_EQ(sequence.phase(), CharacterizationPhase::heat_4);

    sequence.update(90'800, 220.0F);
    sequence.update(120'800, 225.0F);
    EXPECT_EQ(sequence.phase(), CharacterizationPhase::final_heat);
}

TEST(CharacterizationSequenceTest, ReachesCooldownAndCompletesAfterCooling) {
    CharacterizationSequence sequence;
    sequence.start(0);
    sequence.update(30'000, 25.0F);
    sequence.update(30'001, 80.0F);
    sequence.update(50'001, 85.0F);
    sequence.update(50'002, 130.0F);
    sequence.update(70'002, 135.0F);
    sequence.update(70'003, 180.0F);
    sequence.update(90'003, 185.0F);
    sequence.update(90'004, 220.0F);
    sequence.update(120'004, 225.0F);
    sequence.update(120'005, 240.0F);
    EXPECT_EQ(sequence.phase(), CharacterizationPhase::cooldown);
    EXPECT_FALSE(sequence.heater_enabled());

    sequence.update(120'006, 50.0F);
    EXPECT_EQ(sequence.phase(), CharacterizationPhase::completed);
}

TEST(CharacterizationSequenceTest, UserAbortDisablesTheHeaterPermanently) {
    CharacterizationSequence sequence;
    sequence.start(0);
    sequence.update(30'000, 25.0F);
    sequence.abort();
    sequence.update(31'000, 20.0F);

    EXPECT_EQ(sequence.phase(), CharacterizationPhase::aborted);
    EXPECT_EQ(sequence.stop_reason(), CharacterizationStopReason::user_abort);
    EXPECT_FALSE(sequence.heater_enabled());
}

TEST(CharacterizationSequenceTest, SafetyFailuresDisableTheHeater) {
    CharacterizationSequence overtemperature;
    overtemperature.start(0);
    overtemperature.update(30'000, 25.0F);
    overtemperature.update(30'100, 250.0F);
    EXPECT_EQ(overtemperature.phase(), CharacterizationPhase::error);
    EXPECT_EQ(overtemperature.stop_reason(), CharacterizationStopReason::overtemperature);
    EXPECT_FALSE(overtemperature.heater_enabled());

    CharacterizationSequence sensor_error;
    sensor_error.start(0);
    sensor_error.fail(CharacterizationStopReason::sensor_error);
    EXPECT_EQ(sensor_error.phase(), CharacterizationPhase::error);
    EXPECT_FALSE(sensor_error.heater_enabled());
}

TEST(CharacterizationSequenceTest, TimesOutAfterTwentyMinutes) {
    CharacterizationSequence sequence;
    sequence.start(0);
    sequence.update(CharacterizationSequence::MAX_DURATION_MS, 25.0F);

    EXPECT_EQ(sequence.phase(), CharacterizationPhase::error);
    EXPECT_EQ(sequence.stop_reason(), CharacterizationStopReason::timeout);
    EXPECT_FALSE(sequence.heater_enabled());
}

}  // namespace
}  // namespace reflowCtrl
