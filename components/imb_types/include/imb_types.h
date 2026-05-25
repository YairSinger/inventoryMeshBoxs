#pragma once

#include <stdint.h>

#define IMB_UID_LEN            15   /* 7-byte NFC UID as 14 hex chars + null terminator */
#define IMB_NAME_LEN           32   /* max human-readable item name length including null */
#define IMB_REGISTRY_MAX_ITEMS 64   /* hard upper bound on items per box; used for stack buffers */
#define IMB_MESH_MAX_BOXES      8   /* max boxes in one mesh */
#define IMB_REPORT_MAX_ENTRIES (IMB_MESH_MAX_BOXES * IMB_REGISTRY_MAX_ITEMS)  /* 512; mesh-wide report */

/* Operational mode — persisted in NVS imb_state namespace and carried in BLE protocol. */
typedef enum {
    IMB_MODE_SETUP        = 0,  /* first boot, awaiting PIN + box name from phone */
    IMB_MODE_FIELD_CHECK  = 1,  /* default after registration; lid-open triggers scan */
    IMB_MODE_REGISTRATION = 2,  /* active registration session; tags accepted/named */
} imb_op_mode_e;

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
