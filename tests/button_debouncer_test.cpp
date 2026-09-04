#include "button_debouncer.hpp"

#include "gtest/gtest.h"

namespace reflowCtrl {
namespace {

TEST(ButtonDebouncerTest, DoesNotEmitEventForInitialState) {
    ButtonDebouncer debouncer(4);

    EXPECT_FALSE(debouncer.update(false));
    EXPECT_FALSE(debouncer.update(true));
    EXPECT_FALSE(debouncer.update(true));
    EXPECT_FALSE(debouncer.update(true));
    EXPECT_TRUE(debouncer.update(true));
}

TEST(ButtonDebouncerTest, IgnoresBouncingPresses) {
    ButtonDebouncer debouncer(4);
    EXPECT_FALSE(debouncer.update(false));

    EXPECT_FALSE(debouncer.update(true));
    EXPECT_FALSE(debouncer.update(false));
    EXPECT_FALSE(debouncer.update(true));
    EXPECT_FALSE(debouncer.update(false));
    EXPECT_FALSE(debouncer.update(true));
    EXPECT_FALSE(debouncer.update(true));
    EXPECT_FALSE(debouncer.update(true));
    EXPECT_TRUE(debouncer.update(true));
}

TEST(ButtonDebouncerTest, EmitsOneEventPerPress) {
    ButtonDebouncer debouncer(2);
    EXPECT_FALSE(debouncer.update(false));

    EXPECT_FALSE(debouncer.update(true));
    EXPECT_TRUE(debouncer.update(true));
    EXPECT_FALSE(debouncer.update(true));
    EXPECT_FALSE(debouncer.update(true));
    EXPECT_FALSE(debouncer.update(false));
    EXPECT_FALSE(debouncer.update(false));
    EXPECT_FALSE(debouncer.update(true));
    EXPECT_TRUE(debouncer.update(true));
}

}  // namespace
}  // namespace reflowCtrl
