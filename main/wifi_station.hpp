#pragma once

#include "esp_err.h"

namespace reflowCtrl {

esp_err_t start_wifi_station();
void wait_for_wifi_connection();
bool is_wifi_connected();

}  // namespace reflowCtrl
