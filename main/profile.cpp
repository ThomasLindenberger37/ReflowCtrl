#include "profile.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>

namespace reflowCtrl {
namespace {

constexpr double AMBIENT_TEMPERATURE_C = 25.0;
constexpr double WARNING_MARGIN = 0.9;

ProfileConfiguration hxp602_default() {
    // Conservative low-temperature profile for HXP-602 paste.
    return {"HXP-602", 2.0, 140.0, 155.0, 90.0, 165.0, 180.0, 40.0, 3.0};
}

ProfileConfiguration sac_default() {
    // Generic lead-free SAC process targets, independent of a specific alloy vendor.
    return {"SAC Lead-Free", 2.0, 150.0, 175.0, 100.0, 217.0, 240.0, 55.0, 3.0};
}

ProfileSlot custom_slot(const int number) {
    return {std::format("custom-{}", number),
            ProfileType::Custom,
            false,
            {std::format("Custom {}", number), 2.0, 140.0, 160.0, 90.0, 217.0, 235.0, 45.0, 3.0}};
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
              std::format("Requested {} {:.2f} °C/s exceeds oven capability {:.2f} °C/s.", label,
                          requested, capability));
    } else if (requested > capability * WARNING_MARGIN) {
        issue(issues, ValidationSeverity::Warning, field,
              std::format("Requested {} {:.2f} °C/s is close to oven capability {:.2f} °C/s.",
                          label, requested, capability));
    }
}

void append_segment(std::vector<CurvePoint>& points, double& time, const double from,
                    const double to, const double duration, const char* phase) {
    const double start = time;
    const int whole_seconds = std::max(1, static_cast<int>(std::ceil(duration)));
    for (int second = 1; second <= whole_seconds; ++second) {
        const double progress = std::min(1.0, static_cast<double>(second) / duration);
        time = std::min(start + second, start + duration);
        points.push_back({time, from + ((to - from) * progress), phase});
    }
}

}  // namespace

ProfileCollection default_profile_collection() {
    ProfileCollection result;
    result.profiles[0] = {"builtin-hxp602", ProfileType::BuiltIn, true, hxp602_default()};
    result.profiles[1] = {"builtin-sac", ProfileType::BuiltIn, true, sac_default()};
    for (int index = 0; index < 6; ++index) {
        result.profiles[static_cast<std::size_t>(index + 2)] = custom_slot(index + 1);
    }
    return result;
}

std::vector<ValidationIssue> validate_profile(const ProfileConfiguration& profile,
                                              const std::optional<OvenCapabilities>& oven) {
    std::vector<ValidationIssue> issues;
    const auto positive = [&](const double value, const char* field, const char* label) {
        if (!std::isfinite(value) || value <= 0.0) {
            issue(issues, ValidationSeverity::Error, field,
                  std::format("{} must be greater than zero.", label));
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
    if (profile.max_ramp_rate_c_per_s > 0.0
        && profile.time_above_liquidus_s
               <= 2.0 * (profile.peak_temperature_c - profile.liquidus_temperature_c)
                      / profile.max_ramp_rate_c_per_s) {
        issue(issues, ValidationSeverity::Error, "reflow.time_above_liquidus_s",
              "Time above liquidus is too short to reach and leave the peak at this ramp rate.");
    }
    if (oven) {
        if (oven->maximum_temperature_c
            && profile.peak_temperature_c > *oven->maximum_temperature_c) {
            issue(issues, ValidationSeverity::Error, "reflow.peak_temperature_c",
                  std::format("Peak {:.1f} °C exceeds the characterized maximum {:.1f} °C.",
                              profile.peak_temperature_c, *oven->maximum_temperature_c));
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
    result.points.push_back({0.0, ambient, "ramp-up"});
    append_segment(result.points, time, ambient, profile.soak_start_temperature_c,
                   (profile.soak_start_temperature_c - ambient) / profile.max_ramp_rate_c_per_s,
                   "ramp-up");
    append_segment(result.points, time, profile.soak_start_temperature_c,
                   profile.soak_end_temperature_c, profile.soak_duration_s, "soak");
    append_segment(result.points, time, profile.soak_end_temperature_c,
                   profile.liquidus_temperature_c,
                   (profile.liquidus_temperature_c - profile.soak_end_temperature_c)
                       / profile.max_ramp_rate_c_per_s,
                   "ramp-to-reflow");
    const double half_tal = profile.time_above_liquidus_s / 2.0;
    append_segment(result.points, time, profile.liquidus_temperature_c, profile.peak_temperature_c,
                   half_tal, "above-liquidus");
    append_segment(result.points, time, profile.peak_temperature_c, profile.liquidus_temperature_c,
                   half_tal, "peak-and-descent");
    append_segment(result.points, time, profile.liquidus_temperature_c, ambient,
                   (profile.liquidus_temperature_c - ambient) / profile.max_cooling_rate_c_per_s,
                   "cooling");
    result.duration_s = time;
    return result;
}

}  // namespace reflowCtrl
