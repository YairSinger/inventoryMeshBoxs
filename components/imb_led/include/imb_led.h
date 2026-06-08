#pragma once

#include <stdint.h>

typedef struct {
    void (*set_color)(uint8_t r, uint8_t g, uint8_t b);
    void (*schedule_ms)(uint32_t ms, void (*cb)(void *), void *arg);
    void (*cancel)(void);
} imb_led_hal_t;

typedef enum {
    IMB_LED_TAG_INSERT = 0, /* green single pulse */
    IMB_LED_TAG_EXTRACT,    /* red single pulse */
    IMB_LED_AMBIGUOUS,      /* yellow single flash */
    IMB_LED_REG_PASS,       /* solid green 2 s */
    IMB_LED_REG_FAIL,       /* red double-flash */
    IMB_LED_MESH_DISC,      /* blue slow breathing, continuous */
    IMB_LED_BLE_IDLE,       /* dim white pulse every 3 s, continuous */
    IMB_LED_FACTORY_HOLD,   /* slow red breathing, continuous */
    IMB_LED_FACTORY_RESET,  /* fast red flash, continuous */
    IMB_LED_SLEEP,          /* off immediately */
} imb_led_pattern_e;

void imb_led_init(const imb_led_hal_t *hal);
void imb_led_play(imb_led_pattern_e pattern);
void imb_led_stop(void);
