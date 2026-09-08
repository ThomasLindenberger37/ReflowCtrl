idf_component_register(
    SRCS
        "main.cpp"
        "characterization_configuration.cpp"
        "characterization_storage.cpp"
        "message_bus.cpp"
        "components/button.cpp"
        "components/button_press_logger.cpp"
        "components/characterization_controller.cpp"
        "components/characterization_sequence.cpp"
        "components/max6675.cpp"
        "components/characterization_live_feed.cpp"
        "components/relay_output.cpp"
        "components/status_led.cpp"
        "components/temperature_acquisition.cpp"
        "hardware/gpio_pins.cpp"
        "hardware/hardware_configuration.cpp"
        "ota_updater.cpp"
        "web_server.cpp"
        "wifi_station.cpp"
    INCLUDE_DIRS "."
    EMBED_TXTFILES
        "../webserver/frontend/index.html"
        "../webserver/frontend/style.css"
        "../webserver/frontend/app.js"
        "../webserver/frontend/characterization-analyzer.js"
        "../webserver/frontend/reflow-profile.js"
    REQUIRES
        app_update
        bootloader_support
        esp_driver_gpio
        esp_driver_spi
        esp_event
        esp_http_client
        esp_http_server
        esp_https_ota
        esp_netif
        esp_timer
        esp_wifi
        nvs_flash
)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_target(
    message_bus_subscription_check ALL
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/analyze_message_bus_subscriptions.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Checking MessageBus subscription capacity"
)
