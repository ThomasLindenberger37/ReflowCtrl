cmake_minimum_required(VERSION 3.20)

project(reflowCtrl_unit_tests LANGUAGES CXX)

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
    ../main/message_bus.cpp
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
