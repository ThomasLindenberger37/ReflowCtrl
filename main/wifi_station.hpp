#pragma once

#include "esp_err.h"

namespace reflowCtrl {

esp_err_t start_wifi_station();
bool is_wifi_connected();

}  // namespace reflowCtrl
