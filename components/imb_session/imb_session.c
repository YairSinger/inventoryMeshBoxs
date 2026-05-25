#include "imb_session.h"
#include <string.h>

void imb_session_init(imb_session_t *s)
{
    memset(s, 0, sizeof(*s));
}

void imb_session_reset(imb_session_t *s)
{
    memset(s, 0, sizeof(*s));
}

static int find_uid(imb_entry_u *arr, uint16_t count, const char *uid)
{
    for (uint16_t i = 0; i < count; i++) {
        if (strncmp(arr[i].uid, uid, IMB_UID_LEN) == 0) return i;
    }
    return -1;
}

static void remove_at(imb_entry_u *arr, uint16_t *count, uint16_t idx)
{
    arr[idx] = arr[*count - 1];
    (*count)--;
}

void imb_session_apply(imb_session_t *s, const imb_scan_event_t *event)
{
    if (event->dir == IMB_INSERT) {
        if (find_uid(s->present, s->present_count, event->uid) < 0 &&
            s->present_count < IMB_REGISTRY_MAX_ITEMS) {
            strncpy(s->present[s->present_count].uid, event->uid, IMB_UID_LEN - 1);
            s->present[s->present_count].uid[IMB_UID_LEN - 1] = '\0';
            s->present_count++;
        }
        return;
    }

    if (event->dir == IMB_EXTRACT) {
        int idx = find_uid(s->present, s->present_count, event->uid);
        if (idx >= 0) remove_at(s->present, &s->present_count, (uint16_t)idx);
        return;
    }

    /* IMB_AMBIGUOUS */
    if (find_uid(s->ambiguous, s->ambiguous_count, event->uid) < 0 &&
        s->ambiguous_count < IMB_REGISTRY_MAX_ITEMS) {
        strncpy(s->ambiguous[s->ambiguous_count].uid, event->uid, IMB_UID_LEN - 1);
        s->ambiguous[s->ambiguous_count].uid[IMB_UID_LEN - 1] = '\0';
        s->ambiguous_count++;
    }
}

uint16_t imb_session_get_present(const imb_session_t *s,
                                  imb_entry_u *out, uint16_t max)
{
    uint16_t n = s->present_count < max ? s->present_count : max;
    memcpy(out, s->present, n * sizeof(imb_entry_u));
    return n;
}

uint16_t imb_session_get_ambiguous(const imb_session_t *s,
                                    imb_entry_u *out, uint16_t max)
{
    uint16_t n = s->ambiguous_count < max ? s->ambiguous_count : max;
    memcpy(out, s->ambiguous, n * sizeof(imb_entry_u));
    return n;
}
