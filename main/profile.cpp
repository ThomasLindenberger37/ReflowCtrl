#include "profile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

namespace reflowCtrl {
namespace {

constexpr double AMBIENT_TEMPERATURE_C = 25.0;
constexpr double WARNING_MARGIN = 0.9;
constexpr std::size_t MAXIMUM_PREVIEW_POINTS = 160;
constexpr std::size_t PROFILE_SEGMENT_COUNT = 6;

ProfileConfiguration hxp602_default() {
    // Conservative low-temperature profile for HXP-602 paste.
    return {"HXP-602", 2.0, 140.0, 155.0, 90.0, 165.0, 180.0, 40.0, 3.0};
}

ProfileConfiguration sac_default() {
    // Generic lead-free SAC process targets, independent of a specific alloy vendor.
    return {"SAC Lead-Free", 2.0, 150.0, 175.0, 100.0, 217.0, 240.0, 55.0, 3.0};
}

ProfileSlot custom_slot(const int number) {
    return {"custom-" + std::to_string(number),
            ProfileType::Custom,
            false,
            {"Custom " + std::to_string(number), 2.0, 140.0, 160.0, 90.0, 217.0, 235.0, 45.0, 3.0}};
}

std::string measured_limit_message(const char* subject, const double requested,
                                   const char* relation, const double capability,
                                   const char* unit) {
    std::array<char, 176> text{};
    std::snprintf(text.data(), text.size(), "Requested %s %.2f %s %s oven capability %.2f %s.",
                  subject, requested, unit, relation, capability, unit);
    return text.data();
}

void issue(std::vector<ValidationIssue>& issues, const ValidationSeverity severity,
           std::string field, std::string message) {
    issues.push_back({severity, std::move(field), std::move(message)});
}

double capability_at(const std::vector<CharacterizationRatePoint>& points,
                     const double temperature) {
    if (points.empty()) {
        return std::numeric_limits<double>::infinity();
    }
    const auto nearest =
        std::min_element(points.begin(), points.end(), [&](const auto& left, const auto& right) {
            return std::abs(left.temperature_c - temperature)
                   < std::abs(right.temperature_c - temperature);
        });
    return std::abs(nearest->rate_c_per_s);
}

void validate_rate(std::vector<ValidationIssue>& issues, const char* field, const char* label,
                   const double requested, const double capability) {
    if (!std::isfinite(capability)) {
        return;
    }
    if (requested > capability) {
        issue(issues, ValidationSeverity::Error, field,
              measured_limit_message(label, requested, "exceeds", capability, "°C/s"));
    } else if (requested < capability && requested > capability * WARNING_MARGIN) {
        issue(issues, ValidationSeverity::Warning, field,
              measured_limit_message(label, requested, "is close to", capability, "°C/s"));
    }
}

void append_segment(std::vector<CurvePoint>& points, double& time, const double from,
                    const double to, const double duration, const double sample_period,
                    const char* phase) {
    const double start = time;
    const auto sample_count =
        std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(duration / sample_period)));
    for (std::size_t sample = 1; sample <= sample_count; ++sample) {
        const double progress = static_cast<double>(sample) / static_cast<double>(sample_count);
        time = start + duration * progress;
        points.push_back({time, from + ((to - from) * progress), phase});
    }
}

}  // namespace

ProfileCollection default_profile_collection() {
    ProfileCollection result;
    result.profiles[0] = {"builtin-hxp602", ProfileType::BuiltIn, true, hxp602_default()};
    result.profiles[1] = {"builtin-sac", ProfileType::BuiltIn, true, sac_default()};
    for (std::size_t index = 0; index < 6; ++index) {
        result.profiles[index + 2] = custom_slot(static_cast<int>(index + 1));
    }
    return result;
}

ProfileMutationResult save_profile(ProfileCollection& profiles, const std::string_view id,
                                   const ProfileConfiguration& configuration) {
    const auto slot = std::find_if(profiles.profiles.begin(), profiles.profiles.end(),
                                   [&](const auto& value) { return value.id == id; });
    if (slot == profiles.profiles.end()) {
        return ProfileMutationResult::NotFound;
    }
    slot->configuration = configuration;
    slot->occupied = true;
    return ProfileMutationResult::Success;
}

ProfileMutationResult reset_profile(ProfileCollection& profiles, const std::string_view id) {
    const auto slot = std::find_if(profiles.profiles.begin(), profiles.profiles.end(),
                                   [&](const auto& value) { return value.id == id; });
    if (slot == profiles.profiles.end()) {
        return ProfileMutationResult::NotFound;
    }
    const auto defaults = default_profile_collection();
    const auto index = static_cast<std::size_t>(slot - profiles.profiles.begin());
    *slot = defaults.profiles[index];
    if (!slot->occupied && profiles.active_profile_id == id) {
        profiles.active_profile_id = "builtin-hxp602";
    }
    return ProfileMutationResult::Success;
}

ProfileMutationResult clear_profile(ProfileCollection& profiles, const std::string_view id) {
    const auto slot = std::find_if(profiles.profiles.begin(), profiles.profiles.end(),
                                   [&](const auto& value) { return value.id == id; });
    if (slot == profiles.profiles.end()) {
        return ProfileMutationResult::NotFound;
    }
    if (slot->type != ProfileType::Custom) {
        return ProfileMutationResult::NotAllowed;
    }
    return reset_profile(profiles, id);
}

ProfileMutationResult select_profile(ProfileCollection& profiles, const std::string_view id) {
    const auto slot = std::find_if(profiles.profiles.begin(), profiles.profiles.end(),
                                   [&](const auto& value) { return value.id == id; });
    if (slot == profiles.profiles.end() || !slot->occupied) {
        return ProfileMutationResult::NotFound;
    }
    profiles.active_profile_id = slot->id;
    return ProfileMutationResult::Success;
}

ProfileConfiguration with_maximum_oven_rates(const ProfileConfiguration& profile,
                                             const std::optional<OvenCapabilities>& oven) {
    ProfileConfiguration result = profile;
    if (!oven) {
        return result;
    }
    const double heating_rate = capability_at(oven->heating_rates, profile.peak_temperature_c);
    if (std::isfinite(heating_rate) && heating_rate > 0.0) {
        result.max_ramp_rate_c_per_s = heating_rate;
    }
    const double cooling_rate = capability_at(oven->cooling_rates, profile.liquidus_temperature_c);
    if (std::isfinite(cooling_rate) && cooling_rate > 0.0) {
        result.max_cooling_rate_c_per_s = cooling_rate;
    }
    return result;
}

std::vector<ValidationIssue> validate_profile(const ProfileConfiguration& profile,
                                              const std::optional<OvenCapabilities>& oven) {
    std::vector<ValidationIssue> issues;
    const auto positive = [&](const double value, const char* field, const char* label) {
        if (!std::isfinite(value) || value <= 0.0) {
            issue(issues, ValidationSeverity::Error, field,
                  std::string(label) + " must be greater than zero.");
        }
    };
    if (profile.name.empty() || profile.name.size() > 64) {
        issue(issues, ValidationSeverity::Error, "name", "Name must contain 1 to 64 characters.");
    }
    positive(profile.max_ramp_rate_c_per_s, "max_ramp_rate_c_per_s", "Maximum heating rate");
    positive(profile.soak_duration_s, "soak.duration_s", "Soak duration");
    positive(profile.time_above_liquidus_s, "reflow.time_above_liquidus_s", "Time above liquidus");
    positive(profile.max_cooling_rate_c_per_s, "cooling.max_cooling_rate_c_per_s",
             "Maximum cooling rate");
    if (profile.soak_start_temperature_c <= AMBIENT_TEMPERATURE_C
        || profile.soak_end_temperature_c <= profile.soak_start_temperature_c
        || profile.liquidus_temperature_c <= profile.soak_end_temperature_c
        || profile.peak_temperature_c <= profile.liquidus_temperature_c
        || profile.peak_temperature_c > 350.0) {
        issue(issues, ValidationSeverity::Error, "temperature_order",
              "Temperatures must satisfy ambient < soak start < soak end < liquidus < peak ≤ 350 "
              "°C.");
    }
    if (profile.max_ramp_rate_c_per_s > 0.0 && profile.max_cooling_rate_c_per_s > 0.0
        && profile.time_above_liquidus_s
               < (profile.peak_temperature_c - profile.liquidus_temperature_c)
                     * ((1.0 / profile.max_ramp_rate_c_per_s)
                        + (1.0 / profile.max_cooling_rate_c_per_s))) {
        issue(issues, ValidationSeverity::Error, "reflow.time_above_liquidus_s",
              "Time above liquidus is too short for the configured heating and cooling rates.");
    }
    if (oven) {
        if (oven->maximum_temperature_c
            && profile.peak_temperature_c > *oven->maximum_temperature_c) {
            issue(issues, ValidationSeverity::Error, "reflow.peak_temperature_c",
                  measured_limit_message("peak temperature", profile.peak_temperature_c,
                                         "exceeds characterized maximum",
                                         *oven->maximum_temperature_c, "°C"));
        }
        validate_rate(issues, "max_ramp_rate_c_per_s", "heating rate",
                      profile.max_ramp_rate_c_per_s,
                      capability_at(oven->heating_rates, profile.peak_temperature_c));
        validate_rate(issues, "cooling.max_cooling_rate_c_per_s", "cooling rate",
                      profile.max_cooling_rate_c_per_s,
                      capability_at(oven->cooling_rates, profile.liquidus_temperature_c));
    }
    return issues;
}

ProfilePreview generate_profile_preview(const ProfileConfiguration& profile,
                                        const std::optional<OvenCapabilities>& oven) {
    ProfilePreview result;
    result.issues = validate_profile(profile, oven);
    result.valid = std::none_of(result.issues.begin(), result.issues.end(), [](const auto& value) {
        return value.severity == ValidationSeverity::Error;
    });
    result.peak_temperature_c = profile.peak_temperature_c;
    result.time_above_liquidus_s = profile.time_above_liquidus_s;
    result.max_ramp_rate_c_per_s = profile.max_ramp_rate_c_per_s;
    if (!result.valid) {
        return result;
    }
    double time = 0.0;
    const double ambient = oven ? oven->ambient_temperature_c : AMBIENT_TEMPERATURE_C;
    const double ramp_up_duration =
        (profile.soak_start_temperature_c - ambient) / profile.max_ramp_rate_c_per_s;
    const double ramp_to_reflow_duration =
        (profile.liquidus_temperature_c - profile.soak_end_temperature_c)
        / profile.max_ramp_rate_c_per_s;
    const double peak_delta = profile.peak_temperature_c - profile.liquidus_temperature_c;
    const double minimum_peak_ascent = peak_delta / profile.max_ramp_rate_c_per_s;
    const double minimum_peak_descent = peak_delta / profile.max_cooling_rate_c_per_s;
    const double spare_time =
        profile.time_above_liquidus_s - minimum_peak_ascent - minimum_peak_descent;
    const double peak_ascent_duration = minimum_peak_ascent + (spare_time / 2.0);
    const double peak_descent_duration = minimum_peak_descent + (spare_time / 2.0);
    const double cooling_duration =
        (profile.liquidus_temperature_c - ambient) / profile.max_cooling_rate_c_per_s;
    const double total_duration = ramp_up_duration + profile.soak_duration_s
                                  + ramp_to_reflow_duration + peak_ascent_duration
                                  + peak_descent_duration + cooling_duration;
    const double sample_period = std::max(
        1.0,
        total_duration / static_cast<double>(MAXIMUM_PREVIEW_POINTS - 1 - PROFILE_SEGMENT_COUNT));

    result.points.reserve(MAXIMUM_PREVIEW_POINTS);
    result.points.push_back({0.0, ambient, "ramp-up"});
    append_segment(result.points, time, ambient, profile.soak_start_temperature_c, ramp_up_duration,
                   sample_period, "ramp-up");
    append_segment(result.points, time, profile.soak_start_temperature_c,
                   profile.soak_end_temperature_c, profile.soak_duration_s, sample_period, "soak");
    append_segment(result.points, time, profile.soak_end_temperature_c,
                   profile.liquidus_temperature_c, ramp_to_reflow_duration, sample_period,
                   "ramp-to-reflow");
    append_segment(result.points, time, profile.liquidus_temperature_c, profile.peak_temperature_c,
                   peak_ascent_duration, sample_period, "above-liquidus");
    append_segment(result.points, time, profile.peak_temperature_c, profile.liquidus_temperature_c,
                   peak_descent_duration, sample_period, "peak-and-descent");
    append_segment(result.points, time, profile.liquidus_temperature_c, ambient, cooling_duration,
                   sample_period, "cooling");
    result.duration_s = time;
    return result;
}

}  // namespace reflowCtrl
