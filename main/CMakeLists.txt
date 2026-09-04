idf_component_register(
    SRCS
        "main.cpp"
        "message_bus.cpp"
        "max6675.cpp"
        "ota_updater.cpp"
        "temperature_acquisition.cpp"
        "web_server.cpp"
        "wifi_station.cpp"
    INCLUDE_DIRS "."
    EMBED_TXTFILES
        "../webserver/frontend/index.html"
        "../webserver/frontend/style.css"
        "../webserver/frontend/app.js"
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
