#include "components/relay_output.hpp"

namespace reflowCtrl {

esp_err_t RelayOutput::start() {
    output_.set(false);
    return bus_.subscribe<OtaUpdateStarted>(&RelayOutput::on_ota_started, this) ? ESP_OK
                                                                                : ESP_ERR_NO_MEM;
}

void RelayOutput::on_ota_started(const OtaUpdateStarted&) noexcept {
    output_.set(false);
}

}  // namespace reflowCtrl
