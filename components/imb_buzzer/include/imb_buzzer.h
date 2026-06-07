#pragma once

#include <stdint.h>

typedef struct {
    void (*tone)(uint32_t freq_hz);
    void (*silence)(void);
    void (*schedule_ms)(uint32_t ms, void (*cb)(void *), void *arg);
    void (*cancel)(void);
} imb_buzzer_hal_t;

typedef enum {
    IMB_BUZZ_TAG_PLACED = 0,
    IMB_BUZZ_ITEM_REMOVED,
    IMB_BUZZ_UNKNOWN_TAG,
    IMB_BUZZ_ERROR,
    IMB_BUZZ_BLE_CONNECTED,
    IMB_BUZZ_FACTORY_RESET,
} imb_buzzer_pattern_e;

void imb_buzzer_init(const imb_buzzer_hal_t *hal);
void imb_buzzer_play(imb_buzzer_pattern_e pattern);
void imb_buzzer_silence(void);
