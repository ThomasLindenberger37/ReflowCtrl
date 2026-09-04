#include "ipv4_address.hpp"

#include "gtest/gtest.h"

namespace reflowCtrl {
namespace {

TEST(Ipv4AddressTest, AcceptsValidAddresses) {
    EXPECT_TRUE(is_valid_ipv4_address("0.0.0.0"));
    EXPECT_TRUE(is_valid_ipv4_address("192.168.0.40"));
    EXPECT_TRUE(is_valid_ipv4_address("255.255.255.255"));
}

TEST(Ipv4AddressTest, RejectsMalformedAddresses) {
    EXPECT_FALSE(is_valid_ipv4_address(""));
    EXPECT_FALSE(is_valid_ipv4_address("192.168.0"));
    EXPECT_FALSE(is_valid_ipv4_address("192.168.0.1.2"));
    EXPECT_FALSE(is_valid_ipv4_address("192..0.1"));
    EXPECT_FALSE(is_valid_ipv4_address("256.0.0.1"));
    EXPECT_FALSE(is_valid_ipv4_address("1.2.3.-1"));
    EXPECT_FALSE(is_valid_ipv4_address("0012.2.3.4"));
}

}  // namespace
}  // namespace reflowCtrl
