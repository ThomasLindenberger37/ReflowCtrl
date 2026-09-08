#ifndef REFLOWCTRL_CHARACTERIZATION_CONFIGURATION_HPP
#define REFLOWCTRL_CHARACTERIZATION_CONFIGURATION_HPP

#include <cstddef>
#include <string_view>

namespace reflowCtrl {

constexpr std::size_t MAX_CHARACTERIZATION_CONFIGURATION_SIZE = 8192;
[[nodiscard]] bool is_valid_characterization_configuration(std::string_view json);

}  // namespace reflowCtrl

#endif  // REFLOWCTRL_CHARACTERIZATION_CONFIGURATION_HPP
