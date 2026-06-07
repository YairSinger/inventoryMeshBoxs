idf_component_register(SRCS "main.c" INCLUDE_DIRS "." REQUIRES "imb_buzzer")

target_compile_options(${COMPONENT_LIB} PRIVATE -Wextra -Werror)
