idf_component_register(
    SRCS         "imb_led.c" "imb_led_rmt.c"
    INCLUDE_DIRS "include"
    REQUIRES     "esp_driver_rmt" "freertos"
)
