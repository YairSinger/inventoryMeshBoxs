idf_component_register(SRCS "main.c" INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_driver_gpio)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wextra -Werror)
