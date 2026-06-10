#include "imb_nfc.h"
#include <string.h>
#include <stdio.h>

static imb_nfc_hal_t g_hal;

static size_t build_ndef_text(const char *text, uint8_t *buf, size_t max)
{
    size_t tlen        = strlen(text);
    size_t payload_len = 1 + 2 + tlen;   /* status byte + "en" + text */
    size_t record_len  = 4 + payload_len;
    size_t total       = 1 + 1 + record_len + 1; /* 0x03 + len + record + 0xFE */
    if (total > max || payload_len > 255 || record_len > 255) return 0;

    size_t i = 0;
    buf[i++] = 0x03;
    buf[i++] = (uint8_t)record_len;
    buf[i++] = 0xD1;  /* MB ME SR TNF=001 Well-Known */
    buf[i++] = 0x01;  /* type length */
    buf[i++] = (uint8_t)payload_len;
    buf[i++] = 0x54;  /* type "T" */
    buf[i++] = 0x02;  /* UTF-8, 2-char lang */
    buf[i++] = 0x65;  /* 'e' */
    buf[i++] = 0x6E;  /* 'n' */
    memcpy(buf + i, text, tlen); i += tlen;
    buf[i++] = 0xFE;  /* TLV terminator */
    return i;
}

static void uid_bytes_to_str(const imb_nfc_tag_t *t, char out[IMB_NFC_UID_STR_LEN])
{
    int n = t->uid_len > 7 ? 7 : t->uid_len;
    int i;
    for (i = 0; i < n; i++) snprintf(out + i * 2, 3, "%02X", t->uid_bytes[i]);
    out[i * 2] = '\0';
}

void imb_nfc_init(const imb_nfc_hal_t *hal)
{
    g_hal = *hal;
}

int imb_nfc_scan(uint8_t reader_id, imb_nfc_tag_t *out)
{
    int found = g_hal.scan(reader_id, out, g_hal.ctx);
    if (found) uid_bytes_to_str(out, out->uid_str);
    else       out->uid_str[0] = '\0';
    return found;
}

int imb_nfc_find_by_uid(uint8_t reader_id, const char *target_uid_str,
                        imb_nfc_tag_t *out)
{
    if (!imb_nfc_scan(reader_id, out)) return 0;
    return strcmp(out->uid_str, target_uid_str) == 0;
}

int imb_nfc_write_ndef(uint8_t reader_id, const imb_nfc_tag_t *tag,
                       const char *name)
{
    uint8_t buf[64];
    size_t  len = build_ndef_text(name, buf, sizeof(buf));
    if (len == 0) return 0;
    return g_hal.write_pages(reader_id, tag, 4, buf, len, g_hal.ctx);
}

int imb_nfc_read_ndef(uint8_t reader_id, const imb_nfc_tag_t *tag,
                      char *name_out, size_t name_max)
{
    uint8_t raw[48] = {0};
    if (!g_hal.read_pages(reader_id, tag, 4, raw, sizeof(raw), g_hal.ctx))
        return 0;

    /* Parse NDEF TLV: 0x03 | rec_len | record... | 0xFE */
    if (raw[0] != 0x03) return 0;
    uint8_t rec_len = raw[1];
    if (rec_len < 7 || (size_t)(2 + rec_len) > sizeof(raw)) return 0;

    uint8_t *rec = raw + 2;
    if (rec[0] != 0xD1 || rec[1] != 0x01) return 0;   /* Well-Known Text */
    uint8_t payload_len = rec[2];
    if (rec[3] != 0x54) return 0;                      /* type must be "T" */
    if (payload_len < 3) return 0;                     /* status + lang + ≥1 char */

    uint8_t lang_len = rec[4] & 0x3F;
    size_t  text_len = payload_len - 1 - lang_len;
    if (text_len == 0 || lang_len > (size_t)(payload_len - 1)) return 0;

    uint8_t *text = rec + 5 + lang_len;
    size_t   copy = text_len < name_max - 1 ? text_len : name_max - 1;
    memcpy(name_out, text, copy);
    name_out[copy] = '\0';
    return 1;
}
