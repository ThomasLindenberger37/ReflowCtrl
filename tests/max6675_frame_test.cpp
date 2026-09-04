#include "max6675_frame.hpp"

#include "gtest/gtest.h"

namespace reflowCtrl {
namespace {

TEST(Max6675FrameTest, DecodesTemperature) {
    const auto result = decode_max6675_frame(0x07B0);
    EXPECT_EQ(result.status, Max6675FrameStatus::Valid);
    EXPECT_FLOAT_EQ(result.temperature_celsius, 61.5F);
}

TEST(Max6675FrameTest, DetectsOpenThermocouple) {
    EXPECT_EQ(decode_max6675_frame(0x0004).status, Max6675FrameStatus::ThermocoupleOpen);
}

TEST(Max6675FrameTest, RejectsReservedAndSignBits) {
    EXPECT_EQ(decode_max6675_frame(0x0002).status, Max6675FrameStatus::Invalid);
    EXPECT_EQ(decode_max6675_frame(0x8000).status, Max6675FrameStatus::Invalid);
}

}  // namespace
}  // namespace reflowCtrl
