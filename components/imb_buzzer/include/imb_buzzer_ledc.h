#pragma once

#include "imb_buzzer.h"

/* Initialises the LEDC peripheral and FreeRTOS timer, returns a populated HAL.
   Call once before imb_buzzer_init(). */
imb_buzzer_hal_t imb_buzzer_ledc_init(void);
