#include "components/relay_output.hpp"

namespace reflowCtrl {

esp_err_t RelayOutput::start() {
    output_.set(false);
    return bus_.subscribe<OtaUpdateStarted>(&RelayOutput::on_ota_started, this)
                   && bus_.subscribe<CharacterizationStarted>(
                       &RelayOutput::on_characterization_started, this)
                   && bus_.subscribe<CharacterizationAborted>(
                       &RelayOutput::on_characterization_aborted, this)
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}

void RelayOutput::on_ota_started(const OtaUpdateStarted&) noexcept {
    output_.set(false);
}

void RelayOutput::on_characterization_started(const CharacterizationStarted&) noexcept {
    output_.set(true);
}

void RelayOutput::on_characterization_aborted(const CharacterizationAborted&) noexcept {
    output_.set(false);
}

}  // namespace reflowCtrl
