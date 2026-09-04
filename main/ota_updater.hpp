#pragma once

#include "esp_err.h"
#include "message_bus.hpp"

namespace reflowCtrl {

class OtaUpdater {
   public:
    explicit OtaUpdater(MessageBus& bus) noexcept : bus_(bus) {}

    esp_err_t start();
    void on_update_requested(const OtaUpdateRequested& request);

   private:
    static void task_entry(void* context);
    void task_loop();

    MessageBus& bus_;
    void* request_queue_ = nullptr;
};

}  // namespace reflowCtrl
