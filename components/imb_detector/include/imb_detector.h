#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    IMB_INSERT,
    IMB_EXTRACT,
    IMB_AMBIGUOUS,
} imb_direction_t;

typedef struct {
    char uid[15];           /* 7-byte UID as 14 hex chars + null */
    imb_direction_t dir;
} imb_scan_event_t;

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

void imb_detector_init(imb_detector_t *det,
                       uint32_t window_ms,
                       uint32_t (*get_ms)(void),
                       imb_detector_cb_t on_event,
                       void *ctx);

/* Called by driver layer (FreeRTOS task) when a PN532 detects a tag.
   reader_id: 0 = inner reader, 1 = outer reader. */
void imb_detector_on_reader_event(imb_detector_t *det,
                                  uint8_t reader_id,
                                  const char *uid);

/* Call periodically (e.g. every 10ms) to fire AMBIGUOUS when window expires. */
void imb_detector_tick(imb_detector_t *det);
