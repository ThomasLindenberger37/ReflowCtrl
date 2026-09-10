#ifndef REFLOWCTRL_PROFILE_JSON_HPP
#define REFLOWCTRL_PROFILE_JSON_HPP

#include <optional>
#include <string>
#include <string_view>

#include "profile.hpp"

namespace reflowCtrl {

[[nodiscard]] std::optional<ProfileConfiguration> parse_profile(std::string_view json);
[[nodiscard]] std::optional<ProfileCollection> parse_profile_collection(std::string_view json);
[[nodiscard]] std::string serialize_profile(const ProfileSlot& profile);
[[nodiscard]] std::string serialize_profile_collection(const ProfileCollection& profiles);
[[nodiscard]] std::string serialize_profile_preview(const ProfilePreview& preview);
[[nodiscard]] std::optional<OvenCapabilities> parse_oven_capabilities(std::string_view json);

}  // namespace reflowCtrl

#endif  // REFLOWCTRL_PROFILE_JSON_HPP
