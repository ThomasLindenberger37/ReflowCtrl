#include "profile.hpp"

#include <algorithm>
#include <memory>

#include "cJSON.h"
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
    const auto restored_profiles = restored.value_or(default_profile_collection());
    EXPECT_EQ(restored_profiles.active_profile_id, "custom-1");
    EXPECT_EQ(restored_profiles.profiles[2].configuration.name, "My paste");
    EXPECT_FALSE(parse_profile_collection("{broken"));
    EXPECT_FALSE(parse_profile_collection("{\"version\":2,\"profiles\":[]}"));
}

TEST(ProfileSlots, CustomCanBeSavedSelectedAndCleared) {
    auto profiles = default_profile_collection();
    auto custom = profiles.profiles[2].configuration;
    custom.name = "My custom paste";
    EXPECT_EQ(save_profile(profiles, "custom-1", custom), ProfileMutationResult::Success);
    EXPECT_TRUE(profiles.profiles[2].occupied);
    EXPECT_EQ(select_profile(profiles, "custom-1"), ProfileMutationResult::Success);
    EXPECT_EQ(profiles.active_profile_id, "custom-1");
    EXPECT_EQ(clear_profile(profiles, "custom-1"), ProfileMutationResult::Success);
    EXPECT_FALSE(profiles.profiles[2].occupied);
    EXPECT_EQ(profiles.active_profile_id, "builtin-hxp602");
}

TEST(ProfileSlots, BuiltInCannotBeDeletedAndCanBeRestored) {
    auto profiles = default_profile_collection();
    profiles.profiles[0].configuration.name = "Changed";
    EXPECT_EQ(clear_profile(profiles, "builtin-hxp602"), ProfileMutationResult::NotAllowed);
    EXPECT_EQ(reset_profile(profiles, "builtin-hxp602"), ProfileMutationResult::Success);
    EXPECT_EQ(profiles.profiles[0].configuration.name, "HXP-602");
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

TEST(ProfileValidation, UsesHeatingAndCoolingRatesForMinimumTimeAboveLiquidus) {
    auto profile = default_profile_collection().profiles[0].configuration;
    profile.liquidus_temperature_c = 165.0;
    profile.peak_temperature_c = 190.0;
    profile.max_ramp_rate_c_per_s = 1.0;
    profile.max_cooling_rate_c_per_s = 0.2;
    profile.time_above_liquidus_s = 150.0;

    EXPECT_TRUE(validate_profile(profile, std::nullopt).empty());
    profile.time_above_liquidus_s = 149.9;
    const auto issues = validate_profile(profile, std::nullopt);
    EXPECT_TRUE(std::any_of(issues.begin(), issues.end(), [](const auto& issue) {
        return issue.field == "reflow.time_above_liquidus_s"
               && issue.severity == ValidationSeverity::Error;
    }));
}

TEST(ProfileRates, UsesCharacterizedMaximumRatesAtRelevantTemperatures) {
    const auto profile = default_profile_collection().profiles[0].configuration;
    OvenCapabilities oven;
    oven.heating_rates = {{120.0, 2.5}, {190.0, 1.4}};
    oven.cooling_rates = {{100.0, -0.8}, {165.0, -1.7}};

    const auto effective = with_maximum_oven_rates(profile, oven);

    EXPECT_DOUBLE_EQ(effective.max_ramp_rate_c_per_s, 1.4);
    EXPECT_DOUBLE_EQ(effective.max_cooling_rate_c_per_s, 1.7);
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

TEST(ProfileCurve, LimitsPointCountForLongSlowProfiles) {
    auto profile = default_profile_collection().profiles[0].configuration;
    profile.time_above_liquidus_s = 500.0;
    profile.max_cooling_rate_c_per_s = 0.2;

    const auto preview = generate_profile_preview(profile, std::nullopt);

    ASSERT_TRUE(preview.valid);
    EXPECT_LE(preview.points.size(), 160);
    EXPECT_NEAR(preview.points.back().time_s, preview.duration_s, 0.001);
    EXPECT_EQ(preview.points.back().phase, "cooling");
}

TEST(ProfilePreviewJson, SerializesPointsAndEscapesValidationMessages) {
    ProfilePreview preview;
    preview.valid = false;
    preview.duration_s = 12.5;
    preview.issues.push_back({ValidationSeverity::Error, "reflow.peak", "Invalid \"peak\"\nvalue"});
    preview.points.push_back({0.0, 25.0, "preheat"});
    preview.points.push_back({12.5, 80.0, "soak"});

    const std::string json = serialize_profile_preview(preview);
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_Parse(json.c_str()), &cJSON_Delete);
    ASSERT_NE(root, nullptr);
    EXPECT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root.get(), "valid")));
    EXPECT_EQ(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(root.get(), "errors")), 1);
    EXPECT_EQ(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(root.get(), "points")), 2);
}

}  // namespace
}  // namespace reflowCtrl
