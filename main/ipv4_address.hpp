#ifndef REFLOWCTRL_IPV4_ADDRESS_HPP
#define REFLOWCTRL_IPV4_ADDRESS_HPP

#include <cstdint>
#include <string_view>

namespace reflowCtrl {

[[nodiscard]] constexpr bool is_valid_ipv4_address(const std::string_view address) noexcept {
    std::uint32_t octet = 0;
    std::uint8_t octet_count = 0;
    std::uint8_t digit_count = 0;

    for (const char character : address) {
        if (character == '.') {
            if (digit_count == 0 || octet_count >= 3) {
                return false;
            }
            ++octet_count;
            octet = 0;
            digit_count = 0;
            continue;
        }
        if (character < '0' || character > '9' || digit_count >= 3) {
            return false;
        }
        octet = octet * 10U + static_cast<std::uint32_t>(character - '0');
        if (octet > 255) {
            return false;
        }
        ++digit_count;
    }
    return octet_count == 3 && digit_count != 0;
}

}  // namespace reflowCtrl

#endif  // REFLOWCTRL_IPV4_ADDRESS_HPP
