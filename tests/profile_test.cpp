#include "profile.hpp"

#include <algorithm>

#include "gtest/gtest.h"
#include "profile_json.hpp"

namespace reflowCtrl {
namespace {

TEST(ProfileDefaults, ContainsTwoBuiltInsAndSixStableCustomSlots) {
    const auto profiles = default_profile_collection();
    EXPECT_EQ(profiles.version, 1);
    EXPECT_EQ(profiles.active_profile_id, "builtin-hxp602");
    EXPECT_EQ(std::count_if(profiles.profiles.begin(), profiles.profiles.end(),
                            [](const auto& slot) {
                                return slot.type == ProfileType::BuiltIn && slot.occupied;
                            }),
              2);
    EXPECT_EQ(std::count_if(profiles.profiles.begin(), profiles.profiles.end(),
                            [](const auto& slot) {
                                return slot.type == ProfileType::Custom && !slot.occupied;
                            }),
              6);
    EXPECT_EQ(profiles.profiles.back().id, "custom-6");
}

TEST(ProfilePersistence, RoundTripsCollectionAndRejectsCorruption) {
    auto profiles = default_profile_collection();
    profiles.profiles[2].occupied = true;
    profiles.profiles[2].configuration.name = "My paste";
    profiles.active_profile_id = "custom-1";
    const auto restored = parse_profile_collection(serialize_profile_collection(profiles));
    ASSERT_TRUE(restored);
    EXPECT_EQ(restored->active_profile_id, "custom-1");
    EXPECT_EQ(restored->profiles[2].configuration.name, "My paste");
    EXPECT_FALSE(parse_profile_collection("{broken"));
    EXPECT_FALSE(parse_profile_collection("{\"version\":2,\"profiles\":[]}"));
}

TEST(ProfileValidation, RejectsImpossibleTemperatureAndRates) {
    auto profile = default_profile_collection().profiles[0].configuration;
    OvenCapabilities oven;
    oven.maximum_temperature_c = 175.0;
    oven.heating_rates = {{180.0, 1.6}};
    oven.cooling_rates = {{165.0, -2.0}};
    const auto issues = validate_profile(profile, oven);
    EXPECT_GE(std::count_if(
                  issues.begin(), issues.end(),
                  [](const auto& issue) { return issue.severity == ValidationSeverity::Error; }),
              3);
}

TEST(ProfileValidation, WarnsNearCharacterizedLimit) {
    auto profile = default_profile_collection().profiles[0].configuration;
    profile.max_ramp_rate_c_per_s = 1.5;
    profile.max_cooling_rate_c_per_s = 1.5;
    OvenCapabilities oven;
    oven.maximum_temperature_c = 220.0;
    oven.heating_rates = {{180.0, 1.6}};
    oven.cooling_rates = {{165.0, -1.6}};
    const auto issues = validate_profile(profile, oven);
    EXPECT_TRUE(std::any_of(issues.begin(), issues.end(), [](const auto& issue) {
        return issue.severity == ValidationSeverity::Warning;
    }));
    EXPECT_FALSE(std::any_of(issues.begin(), issues.end(), [](const auto& issue) {
        return issue.severity == ValidationSeverity::Error;
    }));
}

TEST(ProfileCurve, GeneratesConsistentPhasesAndTimeAboveLiquidus) {
    const auto profile = default_profile_collection().profiles[0].configuration;
    const auto preview = generate_profile_preview(profile, std::nullopt);
    ASSERT_TRUE(preview.valid);
    ASSERT_FALSE(preview.points.empty());
    EXPECT_NEAR(preview.points.back().time_s, preview.duration_s, 0.001);
    EXPECT_EQ(preview.points.back().phase, "cooling");
    const auto first_above = std::find_if(
        preview.points.begin(), preview.points.end(),
        [&](const auto& point) { return point.temperature_c >= profile.liquidus_temperature_c; });
    const auto last_above = std::find_if(
        preview.points.rbegin(), preview.points.rend(),
        [&](const auto& point) { return point.temperature_c >= profile.liquidus_temperature_c; });
    ASSERT_NE(first_above, preview.points.end());
    ASSERT_NE(last_above, preview.points.rend());
    EXPECT_NEAR(last_above->time_s - first_above->time_s, profile.time_above_liquidus_s, 1.0);
}

TEST(ProfileCurve, ExistingSnapshotDoesNotChangeWhenProfileIsEdited) {
    auto profile = default_profile_collection().profiles[0].configuration;
    const auto running_snapshot = generate_profile_preview(profile, std::nullopt);
    profile.peak_temperature_c = 185.0;
    EXPECT_EQ(running_snapshot.peak_temperature_c, 180.0);
}

}  // namespace
}  // namespace reflowCtrl
