#pragma once

#include "esp_err.h"

namespace reflowCtrl {

using OtaStartedCallback = void (*)(void* context);

esp_err_t start_ota_updater(OtaStartedCallback callback, void* callback_context);
esp_err_t trigger_ota_update(const char* server_address);

}  // namespace reflowCtrl
