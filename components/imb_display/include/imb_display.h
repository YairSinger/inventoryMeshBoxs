#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "imb_types.h"
#include "imb_detector.h"

#define IMB_DISPLAY_MESH_UNKNOWN 0xFF

typedef struct {
    void (*draw_text)(uint8_t row, uint8_t col, const char *str);
    void (*clear)(void);
    void (*schedule_ms)(uint32_t ms, void (*cb)(void *), void *arg);
    void (*cancel)(void);
} imb_display_hal_t;

/* Operational context — feeds the idle screen */
typedef struct {
    char          box_name[IMB_NAME_LEN];
    char          last4_mac[5];           /* "ABCD\0" — for SETUP screen */
    imb_op_mode_e op_mode;
    uint8_t       mesh_peer_count;        /* IMB_DISPLAY_MESH_UNKNOWN until Phase 3 */
    bool          phone_connected;
    uint8_t       pending_tag_count;      /* used in REGISTRATION mode */
} imb_display_ctx_t;

/* Box Report — cycling shows MISSING → FOREIGN → AMBIGUOUS, 2 s each */
typedef struct {
    char    missing[IMB_REGISTRY_MAX_ITEMS][IMB_NAME_LEN];
    uint8_t missing_count;
    char    foreign[IMB_REGISTRY_MAX_ITEMS][IMB_NAME_LEN];
    uint8_t foreign_count;
    char    ambiguous[IMB_REGISTRY_MAX_ITEMS][IMB_NAME_LEN]; /* "" = Anonymous Tag */
    uint8_t ambiguous_count;
    uint8_t total_count;
} imb_display_report_t;

void imb_display_init(const imb_display_hal_t *hal);

/* Update operational context; re-renders idle screen if not in a transient state */
void imb_display_update_ctx(const imb_display_ctx_t *ctx);

/* Named detection → 5 s event screen then idle.
   item_name == NULL → Anonymous Tag error screen (held, no timer) */
void imb_display_on_detection(imb_direction_e dir, const char *item_name);

/* Ambiguous Detection (direction unclear after window).
   item_name == NULL → anonymous ambiguous tag */
void imb_display_on_ambiguous(const char *item_name);

/* Cycle: MISSING → FOREIGN → AMBIGUOUS (2 s each) → idle.
   All counts zero → idle immediately. */
void imb_display_on_report(const imb_display_report_t *report);
