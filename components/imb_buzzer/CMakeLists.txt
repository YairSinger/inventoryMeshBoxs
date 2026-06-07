idf_component_register(
    SRCS         "imb_buzzer.c" "imb_buzzer_ledc.c"
    INCLUDE_DIRS "include"
    REQUIRES     "driver" "freertos"
)
