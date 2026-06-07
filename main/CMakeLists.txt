idf_component_register(SRCS "main.c" INCLUDE_DIRS "."
    REQUIRES imb_buzzer imb_detector imb_session esp_timer esp_driver_gpio
             imb_ble imb_ble_session nvs_flash)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wextra -Werror)
