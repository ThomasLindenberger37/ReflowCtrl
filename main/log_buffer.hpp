#ifndef REFLOWCTRL_LOG_BUFFER_HPP
#define REFLOWCTRL_LOG_BUFFER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string_view>

namespace reflowCtrl {

class LogBuffer {
   public:
    static constexpr std::size_t CAPACITY = 24;
    static constexpr std::size_t LINE_SIZE = 192;
    static constexpr std::size_t SNAPSHOT_SIZE = 4;

    struct Entry {
        std::uint32_t id = 0;
        std::array<char, LINE_SIZE> text{};
    };

    struct Snapshot {
        std::array<Entry, SNAPSHOT_SIZE> entries{};
        std::size_t count = 0;
        std::uint32_t next_cursor = 0;
    };

    void append(std::string_view line) noexcept {
        const std::lock_guard guard(mutex_);
        Entry& entry = entries_[next_index_];
        entry.id = ++cursor_;
        const auto length = line.copy(entry.text.data(), entry.text.size() - 1);
        entry.text[length] = '\0';
        next_index_ = (next_index_ + 1) % entries_.size();
        count_ = count_ < entries_.size() ? count_ + 1 : count_;
    }

    [[nodiscard]] Snapshot after(const std::uint32_t cursor) noexcept {
        Snapshot snapshot{};
        snapshot.next_cursor = cursor;
        const std::lock_guard guard(mutex_);
        const std::size_t first = (next_index_ + entries_.size() - count_) % entries_.size();
        for (std::size_t offset = 0; offset < count_; ++offset) {
            const Entry& entry = entries_[(first + offset) % entries_.size()];
            if (entry.id > cursor) {
                snapshot.entries[snapshot.count++] = entry;
                snapshot.next_cursor = entry.id;
                if (snapshot.count == snapshot.entries.size()) {
                    break;
                }
            }
        }
        return snapshot;
    }

   private:
    std::array<Entry, CAPACITY> entries_{};
    std::mutex mutex_;
    std::size_t next_index_ = 0;
    std::size_t count_ = 0;
    std::uint32_t cursor_ = 0;
};

}  // namespace reflowCtrl

#endif  // REFLOWCTRL_LOG_BUFFER_HPP
