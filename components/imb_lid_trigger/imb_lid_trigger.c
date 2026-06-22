#include "imb_lid_trigger.h"

#define IMB_LID_TRIGGER_DEBOUNCE_MS 50U

void imb_lid_trigger_init(imb_lid_trigger_t *trigger,
                          const imb_lid_trigger_hal_t *hal)
{
    trigger->hal = *hal;
    trigger->stable_state = trigger->hal.read_state(trigger->hal.ctx);
    trigger->candidate_state = trigger->stable_state;
    trigger->candidate_since_ms = trigger->hal.now_ms(trigger->hal.ctx);
    trigger->has_candidate = false;
}

imb_lid_state_e imb_lid_trigger_get_state(const imb_lid_trigger_t *trigger)
{
    return trigger->stable_state;
}

bool imb_lid_trigger_poll(imb_lid_trigger_t *trigger,
                          imb_lid_state_e *changed_state)
{
    imb_lid_state_e raw_state = trigger->hal.read_state(trigger->hal.ctx);
    uint32_t now_ms = trigger->hal.now_ms(trigger->hal.ctx);

    if (raw_state == trigger->stable_state) {
        trigger->has_candidate = false;
        trigger->candidate_state = trigger->stable_state;
        return false;
    }

    if (!trigger->has_candidate || raw_state != trigger->candidate_state) {
        trigger->candidate_state = raw_state;
        trigger->candidate_since_ms = now_ms;
        trigger->has_candidate = true;
        return false;
    }

    if ((uint32_t)(now_ms - trigger->candidate_since_ms) < IMB_LID_TRIGGER_DEBOUNCE_MS) {
        return false;
    }

    trigger->stable_state = raw_state;
    trigger->has_candidate = false;
    if (changed_state != 0) {
        *changed_state = raw_state;
    }
    return true;
}
