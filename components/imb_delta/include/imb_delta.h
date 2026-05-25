#pragma once

#include <stdint.h>
#include "imb_types.h"
#include "imb_session.h"
#include "imb_registry.h"

typedef enum {
    IMB_DELTA_PRESENT,    /* in registry + in session present set */
    IMB_DELTA_MISSING,    /* in registry + absent from session present set */
    IMB_DELTA_FOREIGN,    /* in session present set + not in registry */
    IMB_DELTA_AMBIGUOUS,  /* in session ambiguous set; name filled if known to registry */
} imb_delta_status_e;

typedef struct {
    imb_item_t         item;    /* uid always set; name filled for PRESENT, MISSING, and AMBIGUOUS if registered */
    imb_delta_status_e status;
} imb_delta_entry_t;

/* Worst case: IMB_REGISTRY_MAX_ITEMS MISSING
             + IMB_REGISTRY_MAX_ITEMS FOREIGN
             + IMB_REGISTRY_MAX_ITEMS AMBIGUOUS */
#define IMB_DELTA_MAX_ENTRIES (IMB_REGISTRY_MAX_ITEMS * 3)

/* Computes the diff between registry and session state.
   session  — from the current lid-open cycle
   registry — imb_local, always authoritative; non-const to match imb_registry_get
   out      — caller-provided; declare as imb_delta_entry_t out[IMB_DELTA_MAX_ENTRIES]
   max      — capacity of out; pass IMB_DELTA_MAX_ENTRIES
   Returns number of entries written. */
uint16_t imb_delta_compute(const imb_session_t *session,
                           imb_registry_t      *registry,
                           imb_delta_entry_t   *out,
                           uint16_t             max);
