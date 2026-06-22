#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    IMB_LID_CLOSED = 0,
    IMB_LID_OPEN,
} imb_lid_state_e;

typedef struct {
    imb_lid_state_e (*read_state)(void *ctx);
    uint32_t (*now_ms)(void *ctx);
    void *ctx;
} imb_lid_trigger_hal_t;

typedef struct {
    imb_lid_trigger_hal_t hal;
    imb_lid_state_e stable_state;
    imb_lid_state_e candidate_state;
    uint32_t candidate_since_ms;
    bool has_candidate;
} imb_lid_trigger_t;

void imb_lid_trigger_init(imb_lid_trigger_t *trigger,
                          const imb_lid_trigger_hal_t *hal);

imb_lid_state_e imb_lid_trigger_get_state(const imb_lid_trigger_t *trigger);

bool imb_lid_trigger_poll(imb_lid_trigger_t *trigger,
                          imb_lid_state_e *changed_state);
