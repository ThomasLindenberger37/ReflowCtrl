#ifndef REFLOWCTRL_PROFILE_HPP
#define REFLOWCTRL_PROFILE_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace reflowCtrl {

enum class ProfileType : std::uint8_t { BuiltIn, Custom };
enum class ValidationSeverity : std::uint8_t { Warning, Error };
enum class ProfileMutationResult : std::uint8_t { Success, NotFound, NotAllowed };

struct ProfileConfiguration {
    std::string name;
    double max_ramp_rate_c_per_s{};
    double soak_start_temperature_c{};
    double soak_end_temperature_c{};
    double soak_duration_s{};
    double liquidus_temperature_c{};
    double peak_temperature_c{};
    double time_above_liquidus_s{};
    double max_cooling_rate_c_per_s{};
};

struct ProfileSlot {
    std::string id;
    ProfileType type{};
    bool occupied{};
    ProfileConfiguration configuration;
};

struct ProfileCollection {
    static constexpr int SCHEMA_VERSION = 1;
    int version{SCHEMA_VERSION};
    std::string active_profile_id{"builtin-hxp602"};
    std::array<ProfileSlot, 8> profiles;
};

struct CharacterizationRatePoint {
    double temperature_c{};
    double rate_c_per_s{};
};

struct OvenCapabilities {
    double ambient_temperature_c{25.0};
    std::optional<double> maximum_temperature_c;
    std::vector<CharacterizationRatePoint> heating_rates;
    std::vector<CharacterizationRatePoint> cooling_rates;
};

struct ValidationIssue {
    ValidationSeverity severity{};
    std::string field;
    std::string message;
};

struct CurvePoint {
    double time_s{};
    double temperature_c{};
    std::string phase;
};

struct ProfilePreview {
    bool valid{};
    std::vector<ValidationIssue> issues;
    std::vector<CurvePoint> points;
    double duration_s{};
    double peak_temperature_c{};
    double time_above_liquidus_s{};
    double max_ramp_rate_c_per_s{};
};

[[nodiscard]] ProfileCollection default_profile_collection();
[[nodiscard]] ProfileMutationResult save_profile(ProfileCollection& profiles, std::string_view id,
                                                 const ProfileConfiguration& configuration);
[[nodiscard]] ProfileMutationResult reset_profile(ProfileCollection& profiles, std::string_view id);
[[nodiscard]] ProfileMutationResult clear_profile(ProfileCollection& profiles, std::string_view id);
[[nodiscard]] ProfileMutationResult select_profile(ProfileCollection& profiles,
                                                   std::string_view id);
[[nodiscard]] ProfileConfiguration with_maximum_oven_rates(
    const ProfileConfiguration& profile, const std::optional<OvenCapabilities>& oven);
[[nodiscard]] std::vector<ValidationIssue> validate_profile(
    const ProfileConfiguration& profile, const std::optional<OvenCapabilities>& oven);
[[nodiscard]] ProfilePreview generate_profile_preview(const ProfileConfiguration& profile,
                                                      const std::optional<OvenCapabilities>& oven);

}  // namespace reflowCtrl

#endif  // REFLOWCTRL_PROFILE_HPP
