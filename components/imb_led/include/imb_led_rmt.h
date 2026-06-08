#pragma once

#include "imb_led.h"

/* Initialises RMT channel + WS2812B encoder on GPIO 48.
   Returns a HAL struct ready to pass to imb_led_init(). */
imb_led_hal_t imb_led_rmt_init(void);
