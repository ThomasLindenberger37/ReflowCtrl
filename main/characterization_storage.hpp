#ifndef REFLOWCTRL_CHARACTERIZATION_STORAGE_HPP
#define REFLOWCTRL_CHARACTERIZATION_STORAGE_HPP

#include "esp_http_server.h"

namespace reflowCtrl {

esp_err_t register_characterization_storage(httpd_handle_t server);

}  // namespace reflowCtrl

#endif  // REFLOWCTRL_CHARACTERIZATION_STORAGE_HPP
