#ifndef REFLOWCTRL_PROFILE_STORAGE_HPP
#define REFLOWCTRL_PROFILE_STORAGE_HPP

#include "esp_http_server.h"

namespace reflowCtrl {

esp_err_t register_profile_storage(httpd_handle_t server);

}  // namespace reflowCtrl

#endif  // REFLOWCTRL_PROFILE_STORAGE_HPP
