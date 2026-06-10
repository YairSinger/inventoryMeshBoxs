#pragma once

#include "imb_nfc.h"

typedef struct {
    int mosi;
    int miso;
    int sck;
    int cs[2];   /* cs[0] = reader 0 (inner), cs[1] = reader 1 (outer) */
} imb_nfc_pn532_config_t;

/* Initialise bit-bang SPI GPIOs and wake both PN532 readers.
 * Returns a populated HAL ready to pass to imb_nfc_init(). */
imb_nfc_hal_t imb_nfc_pn532_init(const imb_nfc_pn532_config_t *cfg);
