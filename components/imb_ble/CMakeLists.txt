idf_component_register(
    SRCS         "imb_ble.c"
    INCLUDE_DIRS "include"
    REQUIRES     "imb_protocol" "imb_types" "bt" "nvs_flash"
)
