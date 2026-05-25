#pragma once

#include <stdint.h>

#define IMB_UID_LEN   15   /* 7-byte NFC UID as 14 hex chars + null terminator */
#define IMB_NAME_LEN  32   /* max human-readable item name length including null */

/* Shared item record used by registry, session, and delta components. */
typedef struct {
    char uid[IMB_UID_LEN];    /* UID read from NTAG213 tag */
    char name[IMB_NAME_LEN];  /* NDEF text record written to tag during registration */
} imb_item_t;
