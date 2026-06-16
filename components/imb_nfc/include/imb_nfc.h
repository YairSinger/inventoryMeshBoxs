#pragma once

#include <stdint.h>
#include <stddef.h>

#define IMB_NFC_UID_STR_LEN  15   /* 14 hex chars + null */
#define IMB_NFC_NAME_MAX     32   /* max NDEF text payload */

typedef struct {
    uint8_t atqa[2];
    uint8_t sak;
    uint8_t uid_bytes[10];
    uint8_t uid_len;
    char    uid_str[IMB_NFC_UID_STR_LEN];
    int     found;
} imb_nfc_tag_t;

/* HAL — provided by the chip driver (e.g. imb_nfc_pn532) */
typedef struct {
    /* Scan for one ISO 14443-A tag on reader_id; fills *out and sets out->found.
     * Returns 1 if a tag was found, 0 otherwise. */
    int (*scan)(uint8_t reader_id, imb_nfc_tag_t *out, void *ctx);

    /* Read `len` bytes of tag memory into `out`, starting at start_page.
     * HAL dispatches T2T (4-byte pages) vs MIFARE (16-byte blocks) via tag->sak.
     * Returns 1 on success. */
    int (*read_pages)(uint8_t reader_id, const imb_nfc_tag_t *tag,
                      uint8_t start_page, uint8_t *out, size_t len, void *ctx);

    /* Write `len` bytes from `data` to tag memory starting at start_page.
     * HAL dispatches T2T vs MIFARE via tag->sak.
     * Returns 1 on success. */
    int (*write_pages)(uint8_t reader_id, const imb_nfc_tag_t *tag,
                       uint8_t start_page, const uint8_t *data, size_t len, void *ctx);

    void *ctx;
} imb_nfc_hal_t;

void imb_nfc_init(const imb_nfc_hal_t *hal);

/* Scan reader_id for any tag; fills *out. Returns 1 if found. */
int  imb_nfc_scan(uint8_t reader_id, imb_nfc_tag_t *out);

/* Scan reader_id; fills *out only if the tag UID matches target_uid_str.
 * Returns 1 if the matching tag was found. */
int  imb_nfc_find_by_uid(uint8_t reader_id, const char *target_uid_str,
                         imb_nfc_tag_t *out);

/* Write an NDEF Text record containing `name` to the tag.
 * Returns 1 on success, 0 on failure (tag type unsupported or write error). */
int  imb_nfc_write_ndef(uint8_t reader_id, const imb_nfc_tag_t *tag,
                        const char *name);

/* Read an NDEF Text record from the tag; writes null-terminated name into
 * name_out (max name_max bytes including null).
 * Returns 1 if a non-empty name was found, 0 otherwise. */
int  imb_nfc_read_ndef(uint8_t reader_id, const imb_nfc_tag_t *tag,
                       char *name_out, size_t name_max);
