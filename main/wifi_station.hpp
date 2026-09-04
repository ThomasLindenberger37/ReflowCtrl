#pragma once

#include "esp_err.h"
#include "message_bus.hpp"

namespace reflowCtrl {

esp_err_t start_wifi_station(MessageBus& bus);
void wait_for_wifi_connection();
bool is_wifi_connected();

}  // namespace reflowCtrl
