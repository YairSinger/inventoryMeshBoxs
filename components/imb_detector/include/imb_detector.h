#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    IMB_INSERT,
    IMB_EXTRACT,
    IMB_AMBIGUOUS,
} imb_direction_e;

typedef struct {
    char          uid[15];  /* 7-byte UID as 14 hex chars + null */
    imb_direction_e dir;
} imb_scan_event_t;

/* Called when a directional scan event is resolved.
   event — resolved INSERT / EXTRACT / AMBIGUOUS event
   ctx   — opaque pointer supplied at imb_detector_init; passed through unchanged */
typedef void (*imb_detector_cb_t)(const imb_scan_event_t *event, void *ctx);

typedef struct {
    uint32_t            window_ms;
    uint32_t          (*get_ms)(void);
    imb_detector_cb_t   on_event;
    void               *ctx;
    /* internal */
    bool                has_pending;
    uint8_t             pending_reader;
    char                pending_uid[15];
    uint32_t            pending_ts;
} imb_detector_t;

/* window_ms — maximum gap between reader 0 and reader 1 events to count as a pair
   get_ms    — monotonic millisecond clock; injected so logic is testable without hardware
   on_event  — fired once per resolved event (INSERT / EXTRACT / AMBIGUOUS)
   ctx       — passed through to on_event unchanged; use NULL if not needed */
void imb_detector_init(imb_detector_t *det,
                       uint32_t window_ms,
                       uint32_t (*get_ms)(void),
                       imb_detector_cb_t on_event,
                       void *ctx);

/* reader_id — 0 = inner reader (closer to box interior), 1 = outer reader
   uid       — 14 hex-char null-terminated UID string from PN532 */
void imb_detector_on_reader_event(imb_detector_t *det,
                                  uint8_t reader_id,
                                  const char *uid);

/* Call periodically (e.g. every 10 ms) to fire AMBIGUOUS when window expires. */
void imb_detector_tick(imb_detector_t *det);
