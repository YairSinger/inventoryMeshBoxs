idf_component_register(
    SRCS         "imb_nfc.c" "imb_nfc_pn532.c"
    INCLUDE_DIRS "include"
    REQUIRES     "driver" "freertos"
)
