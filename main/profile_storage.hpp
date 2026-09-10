#ifndef REFLOWCTRL_PROFILE_STORAGE_HPP
#define REFLOWCTRL_PROFILE_STORAGE_HPP

#include <optional>

#include "esp_http_server.h"
#include "profile.hpp"

namespace reflowCtrl {

esp_err_t register_profile_storage(httpd_handle_t server);
[[nodiscard]] bool load_active_profile_for_execution(ProfileConfiguration& profile,
                                                     std::optional<OvenCapabilities>& oven);

}  // namespace reflowCtrl

#endif  // REFLOWCTRL_PROFILE_STORAGE_HPP
