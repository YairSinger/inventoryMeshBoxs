#include "imb_detector.h"
#include <string.h>

void imb_detector_init(imb_detector_t *det,
                       uint32_t window_ms,
                       uint32_t (*get_ms)(void),
                       imb_detector_cb_t on_event,
                       void *ctx)
{
    memset(det, 0, sizeof(*det));
    det->window_ms = window_ms;
    det->get_ms    = get_ms;
    det->on_event  = on_event;
    det->ctx       = ctx;
}

static void fire(imb_detector_t *det, imb_direction_t dir, const char *uid)
{
    imb_scan_event_t ev;
    ev.dir = dir;
    strncpy(ev.uid, uid, sizeof(ev.uid) - 1);
    ev.uid[sizeof(ev.uid) - 1] = '\0';
    det->on_event(&ev, det->ctx);
}

static void clear_pending(imb_detector_t *det)
{
    det->has_pending = false;
}

void imb_detector_on_reader_event(imb_detector_t *det,
                                  uint8_t reader_id,
                                  const char *uid)
{
    uint32_t now = det->get_ms();

    if (!det->has_pending) {
        det->has_pending    = true;
        det->pending_reader = reader_id;
        det->pending_ts     = now;
        strncpy(det->pending_uid, uid, sizeof(det->pending_uid) - 1);
        det->pending_uid[sizeof(det->pending_uid) - 1] = '\0';
        return;
    }

    bool same_uid    = strncmp(det->pending_uid, uid, sizeof(det->pending_uid)) == 0;
    bool same_reader = det->pending_reader == reader_id;
    bool in_window   = (now - det->pending_ts) <= det->window_ms;

    if (same_uid && !same_reader && in_window) {
        /* directional pair: reader 0 first = INSERT, reader 1 first = EXTRACT */
        imb_direction_t dir = (det->pending_reader == 0) ? IMB_INSERT : IMB_EXTRACT;
        fire(det, dir, uid);
        clear_pending(det);
        return;
    }

    /* window expired or different uid or same reader: fire AMBIGUOUS for pending */
    fire(det, IMB_AMBIGUOUS, det->pending_uid);
    clear_pending(det);

    /* start fresh with the new event */
    det->has_pending    = true;
    det->pending_reader = reader_id;
    det->pending_ts     = now;
    strncpy(det->pending_uid, uid, sizeof(det->pending_uid) - 1);
    det->pending_uid[sizeof(det->pending_uid) - 1] = '\0';
}

void imb_detector_tick(imb_detector_t *det)
{
    if (!det->has_pending) return;

    uint32_t now = det->get_ms();
    if ((now - det->pending_ts) > det->window_ms) {
        fire(det, IMB_AMBIGUOUS, det->pending_uid);
        clear_pending(det);
    }
}
