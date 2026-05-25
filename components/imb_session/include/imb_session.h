#pragma once

#include <stdint.h>
#include "imb_types.h"
#include "imb_detector.h"

typedef struct {
    imb_entry_u present[IMB_REGISTRY_MAX_ITEMS];
    uint16_t    present_count;
    imb_entry_u ambiguous[IMB_REGISTRY_MAX_ITEMS];
    uint16_t    ambiguous_count;
} imb_session_t;

void     imb_session_init(imb_session_t *s);
void     imb_session_reset(imb_session_t *s);

/* event — resolved scan event from imb_detector; updates present/ambiguous sets */
void     imb_session_apply(imb_session_t *s, const imb_scan_event_t *event);

/* Copies present UIDs into out[0..max-1]; returns count written.
   out — caller-provided array; stack-allocate IMB_REGISTRY_MAX_ITEMS entries */
uint16_t imb_session_get_present(const imb_session_t *s,
                                 imb_entry_u *out, uint16_t max);

/* Copies ambiguous UIDs into out[0..max-1]; returns count written. */
uint16_t imb_session_get_ambiguous(const imb_session_t *s,
                                   imb_entry_u *out, uint16_t max);
