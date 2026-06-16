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

static void fire(imb_detector_t *det, imb_direction_e dir, const char *uid)
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

static void start_pending(imb_detector_t *det, uint8_t reader_id,
                          const char *uid, uint32_t now)
{
    det->has_pending    = true;
    det->pending_reader = reader_id;
    det->pending_ts     = now;
    strncpy(det->pending_uid, uid, sizeof(det->pending_uid) - 1);
    det->pending_uid[sizeof(det->pending_uid) - 1] = '\0';
    if (det->on_first_seen) det->on_first_seen(uid, det->ctx);
}

void imb_detector_on_reader_event(imb_detector_t *det,
                                  uint8_t reader_id,
                                  const char *uid)
{
    uint32_t now = det->get_ms();

    if (!det->has_pending) {
        start_pending(det, reader_id, uid, now);
        return;
    }

    bool same_uid    = strncmp(det->pending_uid, uid, sizeof(det->pending_uid)) == 0;
    bool same_reader = det->pending_reader == reader_id;
    bool in_window   = (now - det->pending_ts) <= det->window_ms;

    if (same_uid && same_reader && in_window) {
        /* Same card still on same reader — ignore, tick() handles window expiry */
        return;
    }

    if (same_uid && !same_reader && in_window) {
        /* Directional pair: reader 1 (outer) first = INSERT, reader 0 (inner) first = EXTRACT */
        imb_direction_e dir = (det->pending_reader == 1) ? IMB_INSERT : IMB_EXTRACT;
        fire(det, dir, uid);
        clear_pending(det);
        return;
    }

    /* Window expired: fire AMBIGUOUS for old pending, then start fresh.
       If still in window (different uid or same reader): silently discard and start fresh —
       AMBIGUOUS only fires when the window truly expires. */
    if (!in_window) {
        fire(det, IMB_AMBIGUOUS, det->pending_uid);
    }
    clear_pending(det);
    start_pending(det, reader_id, uid, now);
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
