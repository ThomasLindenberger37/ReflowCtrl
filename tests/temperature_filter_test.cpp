#include "temperature_filter.hpp"

#include "gtest/gtest.h"

namespace reflowCtrl {
namespace {

TEST(GaussianTemperatureFilterTest, HasNoSamplesInitially) {
    GaussianTemperatureFilter filter;

    EXPECT_FALSE(filter.has_samples());
}

TEST(GaussianTemperatureFilterTest, ReturnsSingleStartupSampleWithoutDelay) {
    GaussianTemperatureFilter filter;

    filter.add_sample(42.5F);

    EXPECT_TRUE(filter.has_samples());
    EXPECT_FLOAT_EQ(filter.filtered_temperature(), 42.5F);
}

TEST(GaussianTemperatureFilterTest, NormalizesStartupWeights) {
    GaussianTemperatureFilter filter;

    filter.add_sample(10.0F);
    filter.add_sample(30.0F);

    EXPECT_NEAR(filter.filtered_temperature(), 15.36F, 0.0001F);
}

TEST(GaussianTemperatureFilterTest, UsesAllFourSamplesAfterStartup) {
    GaussianTemperatureFilter filter;

    for (int temperature = 0; temperature < 4; ++temperature) {
        filter.add_sample(static_cast<float>(temperature * 10));
    }

    EXPECT_NEAR(filter.filtered_temperature(), 15.0F, 0.0001F);
}

TEST(GaussianTemperatureFilterTest, DropsSamplesOlderThanFourIntervals) {
    GaussianTemperatureFilter filter;

    for (int sample = 0; sample < 4; ++sample) {
        filter.add_sample(0.0F);
    }
    filter.add_sample(100.0F);

    EXPECT_NEAR(filter.filtered_temperature(), 13.4F, 0.0001F);
}

}  // namespace
}  // namespace reflowCtrl
