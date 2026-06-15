idf_component_register(
    SRCS        "test_imb_nfc_hw.c"
    REQUIRES    imb_nfc imb_detector unity freertos esp_timer
)
