#include "log_buffer.hpp"

#include <string>

#include "gtest/gtest.h"

namespace reflowCtrl {
namespace {

TEST(LogBufferTest, ReturnsOnlyEntriesAfterCursor) {
    LogBuffer buffer;
    buffer.append("first");
    buffer.append("second");

    const auto snapshot = buffer.after(1);
    ASSERT_EQ(snapshot.count, 1U);
    EXPECT_EQ(snapshot.entries[0].id, 2U);
    EXPECT_STREQ(snapshot.entries[0].text.data(), "second");
    EXPECT_EQ(snapshot.next_cursor, 2U);
}

TEST(LogBufferTest, RetainsNewestEntriesAndTruncatesLongLines) {
    LogBuffer buffer;
    for (std::size_t index = 0; index <= LogBuffer::CAPACITY; ++index) {
        buffer.append(std::to_string(index));
    }
    buffer.append(std::string(LogBuffer::LINE_SIZE + 20, 'x'));

    auto snapshot = buffer.after(0);
    ASSERT_EQ(snapshot.count, LogBuffer::SNAPSHOT_SIZE);
    EXPECT_EQ(snapshot.entries[0].id, 3U);

    while (snapshot.next_cursor < LogBuffer::CAPACITY + 2) {
        snapshot = buffer.after(snapshot.next_cursor);
    }
    ASSERT_GT(snapshot.count, 0U);
    EXPECT_EQ(std::char_traits<char>::length(snapshot.entries[snapshot.count - 1].text.data()),
              LogBuffer::LINE_SIZE - 1);
}

TEST(LogBufferTest, CursorAdvancesOnlyPastReturnedPage) {
    LogBuffer buffer;
    for (std::size_t index = 0; index < LogBuffer::SNAPSHOT_SIZE + 1; ++index) {
        buffer.append(std::to_string(index));
    }

    const auto first = buffer.after(0);
    ASSERT_EQ(first.count, LogBuffer::SNAPSHOT_SIZE);
    EXPECT_EQ(first.next_cursor, LogBuffer::SNAPSHOT_SIZE);

    const auto second = buffer.after(first.next_cursor);
    ASSERT_EQ(second.count, 1U);
    EXPECT_EQ(second.entries[0].id, LogBuffer::SNAPSHOT_SIZE + 1);
}

}  // namespace
}  // namespace reflowCtrl
