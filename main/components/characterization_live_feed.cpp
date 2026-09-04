#include "components/characterization_live_feed.hpp"

#include <cstdio>
#include <cstring>

namespace reflowCtrl {

CharacterizationLiveFeed::CharacterizationLiveFeed() noexcept
    : mutex_(xSemaphoreCreateMutexStatic(&mutex_storage_)) {}

void CharacterizationLiveFeed::reset() noexcept {
    lock();
    preview_count_ = 0;
    first_preview_id_ = next_preview_id_;
    unlock();
}

void CharacterizationLiveFeed::append(const std::uint32_t time_ms, const float temperature_celsius,
                                      const bool heater_output,
                                      const CharacterizationPhase phase) noexcept {
    std::array<char, LINE_SIZE> line{};
    const int length =
        std::snprintf(line.data(), line.size(), "%lu,%.2f,%u,%s",
                      static_cast<unsigned long>(time_ms), static_cast<double>(temperature_celsius),
                      heater_output ? 1U : 0U, characterization_phase_name(phase));
    if (length < 0 || static_cast<std::size_t>(length) >= line.size()) {
        return;
    }
    lock();
    append_preview(line.data());
    unlock();
}

CharacterizationLiveFeed::PreviewSnapshot CharacterizationLiveFeed::preview_after(
    const std::uint32_t cursor) noexcept {
    lock();
    PreviewSnapshot snapshot{};
    const std::uint32_t first_id = preview_count_ == 0 ? next_preview_id_ : first_preview_id_;
    const std::uint32_t start_id = cursor < first_id ? first_id : cursor + 1;
    for (std::uint32_t id = start_id; id < next_preview_id_ && snapshot.count < PREVIEW_CAPACITY;
         ++id) {
        const std::size_t slot = (id - 1U) % PREVIEW_CAPACITY;
        snapshot.lines[snapshot.count++] = preview_[slot];
    }
    snapshot.next_cursor = next_preview_id_ - 1U;
    unlock();
    return snapshot;
}

void CharacterizationLiveFeed::lock() noexcept {
    xSemaphoreTake(mutex_, portMAX_DELAY);
}

void CharacterizationLiveFeed::unlock() noexcept {
    xSemaphoreGive(mutex_);
}

void CharacterizationLiveFeed::append_preview(const char* const line) noexcept {
    const std::size_t slot = (next_preview_id_ - 1U) % PREVIEW_CAPACITY;
    std::strncpy(preview_[slot].data(), line, preview_[slot].size() - 1U);
    preview_[slot].back() = '\0';
    ++next_preview_id_;
    if (preview_count_ < PREVIEW_CAPACITY) {
        ++preview_count_;
    } else {
        ++first_preview_id_;
    }
}

}  // namespace reflowCtrl
