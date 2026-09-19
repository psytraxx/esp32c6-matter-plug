idf_component_register(
    SRCS
        "app_main.cpp"
        "matter_setup.cpp"
        "power_measurement.cpp"
        "bl0937.cpp"
        "relay.cpp"
        "status_led.cpp"
        "button.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver esp_driver_gpio esp_timer
)
