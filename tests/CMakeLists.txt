cmake_minimum_required(VERSION 3.20)

project(reflowCtrl_unit_tests LANGUAGES C CXX)

include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(
    led_blinker_test
    led_blinker_test.cpp
    button_debouncer_test.cpp
    message_bus_test.cpp
    ipv4_address_test.cpp
    log_buffer_test.cpp
    max6675_frame_test.cpp
    temperature_filter_test.cpp
    temperature_controller_test.cpp
    characterization_sequence_test.cpp
    ../main/message_bus.cpp
    ../main/components/characterization_sequence.cpp
    ../main/components/relay_window_controller.cpp
    ../main/components/safety_controller.cpp
    ../main/components/temperature_controller.cpp
    ../main/components/temperature_history.cpp
)
target_compile_features(led_blinker_test PRIVATE cxx_std_23)
target_compile_options(led_blinker_test PRIVATE -Wall -Wextra -Werror -pedantic)
target_include_directories(
    led_blinker_test
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../main
        ${CMAKE_CURRENT_LIST_DIR}/../main/components
)
target_link_libraries(led_blinker_test PRIVATE GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(led_blinker_test)

# Prefer the firmware dependency; host tests also work before an ESP-IDF build.
set(characterization_json_dir "${CMAKE_CURRENT_LIST_DIR}/../managed_components/espressif__cjson/cJSON")
if(NOT EXISTS "${characterization_json_dir}/cJSON.c")
    FetchContent_Declare(characterization_cjson
        GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
        GIT_TAG v1.7.19
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR source_only)
    FetchContent_MakeAvailable(characterization_cjson)
    set(characterization_json_dir "${characterization_cjson_SOURCE_DIR}")
endif()
add_library(characterization_json STATIC "${characterization_json_dir}/cJSON.c")
target_include_directories(characterization_json SYSTEM PUBLIC "${characterization_json_dir}")
add_executable(characterization_configuration_test
    characterization_configuration_test.cpp
    ../main/characterization_configuration.cpp)
target_compile_features(characterization_configuration_test PRIVATE cxx_std_23)
target_compile_options(characterization_configuration_test PRIVATE -Wall -Wextra -Werror -pedantic)
target_include_directories(characterization_configuration_test PRIVATE ../main)
target_link_libraries(characterization_configuration_test PRIVATE characterization_json GTest::gtest_main)
gtest_discover_tests(characterization_configuration_test)

add_executable(profile_test
    profile_test.cpp
    ../main/profile.cpp
    ../main/profile_json.cpp)
target_compile_features(profile_test PRIVATE cxx_std_23)
target_compile_options(profile_test PRIVATE -Wall -Wextra -Werror -pedantic)
target_include_directories(profile_test PRIVATE ../main)
target_link_libraries(profile_test PRIVATE characterization_json GTest::gtest_main)
gtest_discover_tests(profile_test)
