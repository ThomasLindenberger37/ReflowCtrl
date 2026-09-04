#pragma once

#include "esp_err.h"

namespace reflowCtrl {

esp_err_t start_ota_updater();
esp_err_t trigger_ota_update(const char* server_address);

}  // namespace reflowCtrl
