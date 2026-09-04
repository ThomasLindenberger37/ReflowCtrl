#pragma once

#include "esp_err.h"

namespace reflowCtrl {

class TemperatureAcquisition;

esp_err_t start_web_server(TemperatureAcquisition& temperature_acquisition);

}  // namespace reflowCtrl
