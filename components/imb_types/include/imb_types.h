#pragma once

#include <stdint.h>

#define IMB_UID_LEN            15   /* 7-byte NFC UID as 14 hex chars + null terminator */
#define IMB_NAME_LEN           32   /* max human-readable item name length including null */
#define IMB_REGISTRY_MAX_ITEMS 64   /* hard upper bound on items per box; used for stack buffers */

/* Shared item record used by registry, session, and delta components. */
typedef struct {
    char uid[IMB_UID_LEN];    /* UID read from NTAG213 tag */
    char name[IMB_NAME_LEN];  /* NDEF text record written to tag during registration */
} imb_item_t;

/* Two-phase entry: session writes uid only; delta/registry fills name in-place.
   uid and item.uid occupy the same memory (item.uid is first field of imb_item_t). */
typedef union {
    char       uid[IMB_UID_LEN];  /* written by imb_session */
    imb_item_t item;              /* uid readable immediately; name filled by registry */
} imb_entry_u;
