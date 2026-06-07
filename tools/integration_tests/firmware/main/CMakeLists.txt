idf_component_register(SRCS "main.c" INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_driver_gpio imb_buzzer)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wextra -Werror)
