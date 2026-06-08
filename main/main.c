/* IMB Node — main application loop
 *
 * Stack: PN532 (bit-bang SPI) → imb_detector → imb_session + imb_buzzer + imb_led + imb_ble_session
 *
 * Wiring (hardware.md):
 *   MOSI=11, MISO=13, SCK=12
 *   CS inner (reader 0) = GPIO10
 *   CS outer (reader 1) = GPIO9
 *   Buzzer = GPIO17 (LEDC PWM, direct drive)
 *   LED    = GPIO48 (WS2812B via RMT)
 */
#include <stdio.h>
#include <string.h>
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "imb_buzzer.h"
#include "imb_buzzer_ledc.h"
#include "imb_led.h"
#include "imb_led_rmt.h"
#include "imb_detector.h"
#include "imb_session.h"
#include "imb_registry.h"
#include "imb_ble.h"
#include "imb_ble_session.h"

/* ── Pin map ────────────────────────────────────────────────────────────── */

#define PIN_MOSI  11
#define PIN_MISO  13
#define PIN_SCK   12
#define PIN_CS0   10   /* inner reader (reader 0) */
#define PIN_CS1    9   /* outer reader (reader 1) */
#define PIN_BOOT   0   /* BOOT button, active-low */

#define FACTORY_RESET_HOLD_MS  10000  /* 10 s hold triggers factory reset */

/* ── NVS helpers ────────────────────────────────────────────────────────── */

#define NVS_NS_IDENTITY   "imb_identity"
#define NVS_NS_STATE      "imb_state"
#define NVS_KEY_PIN_HASH  "pin_hash"
#define NVS_KEY_BOX_NAME  "box_name"
#define NVS_KEY_OP_MODE   "op_mode"
#define NVS_KEY_PEND_UIDS "pending_uids"

static uint32_t nvs_load_pin_hash(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_IDENTITY, NVS_READONLY, &h) != ESP_OK) return 0;
    uint32_t val = 0;
    nvs_get_u32(h, NVS_KEY_PIN_HASH, &val);
    nvs_close(h);
    return val;
}

static void nvs_load_box_name(char *name, size_t maxlen)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_IDENTITY, NVS_READONLY, &h) != ESP_OK) return;
    nvs_get_str(h, NVS_KEY_BOX_NAME, name, &maxlen);
    nvs_close(h);
}

static void nvs_save_identity(uint32_t pin_hash, const char *box_name)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_IDENTITY, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u32(h, NVS_KEY_PIN_HASH, pin_hash);
    nvs_set_str(h, NVS_KEY_BOX_NAME, box_name);
    nvs_commit(h);
    nvs_close(h);
    printf("[NVS] identity saved — pin_hash=0x%08lX  name=%s\n",
           (unsigned long)pin_hash, box_name);
}

static void nvs_save_box_name(uint32_t pin_hash, const char *box_name)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_IDENTITY, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, NVS_KEY_BOX_NAME, box_name);
    nvs_commit(h);
    nvs_close(h);
    printf("[NVS] box_name saved → %s\n", box_name);
    (void)pin_hash;
}

/* ── NVS HAL (wired to imb_registry → imb_local namespace) ─────────────── */

#define NVS_NS_LOCAL "imb_local"

static imb_reg_err_e reg_nvs_load(const char *key, imb_item_t *out, void *ctx)
{
    (void)ctx;
    nvs_handle_t h;
    if (nvs_open(NVS_NS_LOCAL, NVS_READONLY, &h) != ESP_OK) return IMB_REG_ERR_HAL;
    size_t len = sizeof(imb_item_t);
    esp_err_t err = nvs_get_blob(h, key, out, &len);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return IMB_REG_ERR_NOT_FOUND;
    return err == ESP_OK ? IMB_REG_OK : IMB_REG_ERR_HAL;
}

static imb_reg_err_e reg_nvs_save(const char *key, const imb_item_t *in, void *ctx)
{
    (void)ctx;
    nvs_handle_t h;
    if (nvs_open(NVS_NS_LOCAL, NVS_READWRITE, &h) != ESP_OK) return IMB_REG_ERR_HAL;
    esp_err_t err = nvs_set_blob(h, key, in, sizeof(imb_item_t));
    if (err == ESP_OK) nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK ? IMB_REG_OK : IMB_REG_ERR_HAL;
}

static imb_reg_err_e reg_nvs_erase(const char *key, void *ctx)
{
    (void)ctx;
    nvs_handle_t h;
    if (nvs_open(NVS_NS_LOCAL, NVS_READWRITE, &h) != ESP_OK) return IMB_REG_ERR_HAL;
    esp_err_t err = nvs_erase_key(h, key);
    if (err == ESP_OK) nvs_commit(h);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return IMB_REG_ERR_NOT_FOUND;
    return err == ESP_OK ? IMB_REG_OK : IMB_REG_ERR_HAL;
}

static imb_reg_err_e reg_nvs_load_u16(const char *key, uint16_t *out, void *ctx)
{
    (void)ctx;
    nvs_handle_t h;
    if (nvs_open(NVS_NS_LOCAL, NVS_READONLY, &h) != ESP_OK) return IMB_REG_ERR_HAL;
    esp_err_t err = nvs_get_u16(h, key, out);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return IMB_REG_ERR_NOT_FOUND;
    return err == ESP_OK ? IMB_REG_OK : IMB_REG_ERR_HAL;
}

static imb_reg_err_e reg_nvs_save_u16(const char *key, uint16_t val, void *ctx)
{
    (void)ctx;
    nvs_handle_t h;
    if (nvs_open(NVS_NS_LOCAL, NVS_READWRITE, &h) != ESP_OK) return IMB_REG_ERR_HAL;
    esp_err_t err = nvs_set_u16(h, key, val);
    if (err == ESP_OK) nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK ? IMB_REG_OK : IMB_REG_ERR_HAL;
}

static imb_nvs_hal_t g_reg_nvs_hal = {
    .load     = reg_nvs_load,
    .save     = reg_nvs_save,
    .erase    = reg_nvs_erase,
    .load_u16 = reg_nvs_load_u16,
    .save_u16 = reg_nvs_save_u16,
    .ctx      = NULL,
};

/* ── NVS HAL (wired to imb_ble_session) ─────────────────────────────────── */

static int hal_nvs_read_op_mode(imb_op_mode_e *out, void *ctx)
{
    (void)ctx;
    nvs_handle_t h;
    if (nvs_open(NVS_NS_STATE, NVS_READONLY, &h) != ESP_OK) return -1;
    uint8_t val;
    esp_err_t err = nvs_get_u8(h, NVS_KEY_OP_MODE, &val);
    nvs_close(h);
    if (err != ESP_OK) return -1;
    *out = (imb_op_mode_e)val;
    return 0;
}

static int hal_nvs_write_op_mode(imb_op_mode_e mode, void *ctx)
{
    (void)ctx;
    nvs_handle_t h;
    if (nvs_open(NVS_NS_STATE, NVS_READWRITE, &h) != ESP_OK) return -1;
    esp_err_t err = nvs_set_u8(h, NVS_KEY_OP_MODE, (uint8_t)mode);
    if (err == ESP_OK) nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK ? 0 : -1;
}

static int hal_nvs_read_pending_uids(char uids[][IMB_UID_LEN], uint8_t *count_out, void *ctx)
{
    (void)ctx;
    nvs_handle_t h;
    if (nvs_open(NVS_NS_STATE, NVS_READONLY, &h) != ESP_OK) { *count_out = 0; return -1; }
    size_t len = IMB_REGISTRY_MAX_ITEMS * IMB_UID_LEN;
    esp_err_t err = nvs_get_blob(h, NVS_KEY_PEND_UIDS, uids, &len);
    nvs_close(h);
    if (err != ESP_OK) { *count_out = 0; return -1; }
    *count_out = (uint8_t)(len / IMB_UID_LEN);
    return 0;
}

static int hal_nvs_write_pending_uids(const char uids[][IMB_UID_LEN], uint8_t count, void *ctx)
{
    (void)ctx;
    nvs_handle_t h;
    if (nvs_open(NVS_NS_STATE, NVS_READWRITE, &h) != ESP_OK) return -1;
    esp_err_t err = nvs_set_blob(h, NVS_KEY_PEND_UIDS, uids, count * IMB_UID_LEN);
    if (err == ESP_OK) nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK ? 0 : -1;
}

static const imb_ble_session_nvs_hal_t g_nvs_hal = {
    .read_op_mode       = hal_nvs_read_op_mode,
    .write_op_mode      = hal_nvs_write_op_mode,
    .read_pending_uids  = hal_nvs_read_pending_uids,
    .write_pending_uids = hal_nvs_write_pending_uids,
};

/* ── PN532 bit-bang SPI (LSB-first, mode 0) ─────────────────────────────── */

static const uint8_t k_sam_config[] = {
    0x00,0x00,0xFF,0x05,0xFB, 0xD4,0x14,0x01,0x00,0x00, 0x17,0x00
};
static const uint8_t k_rfconfig[] = {
    0x00,0x00,0xFF,0x06,0xFA, 0xD4,0x32,0x05,0xFF,0xFF,0x03, 0xF4,0x00
};
static const uint8_t k_14443a[] = {
    0x00,0x00,0xFF,0x04,0xFC, 0xD4,0x4A,0x01,0x00, 0xE1,0x00
};

static uint8_t bb_byte(uint8_t out)
{
    uint8_t in = 0;
    for (int i = 0; i < 8; i++) {
        gpio_set_level(PIN_MOSI, (out >> i) & 1);
        ets_delay_us(2);
        gpio_set_level(PIN_SCK, 1);
        ets_delay_us(2);
        if (gpio_get_level(PIN_MISO)) in |= (1 << i);
        gpio_set_level(PIN_SCK, 0);
        ets_delay_us(2);
    }
    return in;
}

static void bb_write(int cs, const uint8_t *buf, size_t len)
{
    gpio_set_level(cs, 0); ets_delay_us(5);
    for (size_t i = 0; i < len; i++) bb_byte(buf[i]);
    ets_delay_us(5); gpio_set_level(cs, 1);
}

static void bb_read(int cs, uint8_t cmd, uint8_t *rx, size_t len)
{
    gpio_set_level(cs, 0); ets_delay_us(5);
    bb_byte(cmd);
    for (size_t i = 0; i < len; i++) rx[i] = bb_byte(0x00);
    ets_delay_us(5); gpio_set_level(cs, 1);
}

static int wait_ready(int cs, int tries, int delay_ms)
{
    for (int i = 0; i < tries; i++) {
        uint8_t s = 0;
        bb_read(cs, 0x02, &s, 1);
        if (s == 0x01) return 1;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    return 0;
}

static void send_recv(int cs, const uint8_t *frame, size_t flen,
                      uint8_t *resp, size_t rlen)
{
    uint8_t wbuf[64];
    wbuf[0] = 0x01; /* DATAWRITE */
    memcpy(wbuf + 1, frame, flen);
    bb_write(cs, wbuf, flen + 1);
    if (!wait_ready(cs, 50, 10)) return;
    uint8_t ack[6] = {0}; bb_read(cs, 0x03, ack, 6);
    if (!wait_ready(cs, 50, 10)) return;
    bb_read(cs, 0x03, resp, rlen);
}

static void pn532_wakeup(int cs)
{
    gpio_set_level(cs, 0); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(cs, 1); vTaskDelay(pdMS_TO_TICKS(5));
}

static void pn532_init(int cs)
{
    uint8_t r[8] = {0};
    send_recv(cs, k_sam_config, sizeof(k_sam_config), r, sizeof(r));
    send_recv(cs, k_rfconfig,   sizeof(k_rfconfig),   r, sizeof(r));
}

typedef struct {
    uint8_t atqa[2];
    uint8_t sak;
    uint8_t uid[10];
    uint8_t uid_len;
    int     found;
} tag_t;

static tag_t scan_tag(int cs)
{
    tag_t t = {0};
    uint8_t wbuf[sizeof(k_14443a) + 1];
    wbuf[0] = 0x01;
    memcpy(wbuf + 1, k_14443a, sizeof(k_14443a));
    bb_write(cs, wbuf, sizeof(wbuf));
    if (!wait_ready(cs, 50, 10)) return t;
    uint8_t ack[6] = {0}; bb_read(cs, 0x03, ack, 6);
    if (!wait_ready(cs, 50, 10)) return t;
    uint8_t resp[32] = {0}; bb_read(cs, 0x03, resp, sizeof(resp));
    for (int i = 0; i < 28; i++) {
        if (resp[i] == 0xD5 && resp[i+1] == 0x4B && resp[i+2] > 0) {
            t.atqa[0]  = resp[i+4]; t.atqa[1] = resp[i+5];
            t.sak      = resp[i+6];
            t.uid_len  = resp[i+7]; if (t.uid_len > 10) t.uid_len = 10;
            memcpy(t.uid, resp + i + 8, t.uid_len);
            t.found = 1;
            return t;
        }
    }
    return t;
}

static void uid_to_str(const tag_t *t, char out[15])
{
    int n = t->uid_len > 7 ? 7 : t->uid_len;
    int i;
    for (i = 0; i < n; i++) snprintf(out + i * 2, 3, "%02X", t->uid[i]);
    out[i * 2] = '\0';
}

/* ── NDEF + PN532 frame helpers ─────────────────────────────────────────── */

/* payload = TFI (0xD4) + command bytes */
static size_t build_frame(uint8_t *out, const uint8_t *payload, size_t plen)
{
    out[0] = 0x00; out[1] = 0x00; out[2] = 0xFF;
    out[3] = (uint8_t)plen;
    out[4] = (uint8_t)(256 - plen);
    memcpy(out + 5, payload, plen);
    uint8_t dcs = 0;
    for (size_t i = 0; i < plen; i++) dcs += payload[i];
    out[5 + plen] = (uint8_t)(256 - dcs);
    out[6 + plen] = 0x00;
    return 7 + plen;
}

/* Build an NDEF Text record TLV (UTF-8, lang "en") into buf.
 * Returns bytes written, 0 if buf too small. */
static size_t build_ndef_text(const char *text, uint8_t *buf, size_t max)
{
    size_t tlen        = strlen(text);
    size_t payload_len = 1 + 2 + tlen;  /* status + "en" + text */
    size_t record_len  = 4 + payload_len;
    size_t total       = 1 + 1 + record_len + 1;  /* 0x03 + len + record + 0xFE */
    if (total > max || payload_len > 255 || record_len > 255) return 0;

    size_t i = 0;
    buf[i++] = 0x03;
    buf[i++] = (uint8_t)record_len;
    buf[i++] = 0xD1;  /* MB ME SR TNF=001 Well-Known */
    buf[i++] = 0x01;  /* type length */
    buf[i++] = (uint8_t)payload_len;
    buf[i++] = 0x54;  /* "T" */
    buf[i++] = 0x02;  /* UTF-8, 2-char lang code */
    buf[i++] = 0x65;  /* 'e' */
    buf[i++] = 0x6E;  /* 'n' */
    memcpy(buf + i, text, tlen); i += tlen;
    buf[i++] = 0xFE;  /* TLV terminator */
    return i;
}

/* Write NDEF text record to tag on cs. Returns 1 on success, 0 on failure.
 * Supports NTAG213 (ATQA=4400 SAK=00) and MIFARE Classic gen1a (ATQA=0004 SAK=08). */
static int ndef_write(int cs, const tag_t *tag, const char *text)
{
    uint8_t ndef[64];
    size_t  ndef_len = build_ndef_text(text, ndef, sizeof(ndef));
    if (ndef_len == 0) return 0;

    /* T2T family (NTAG213/215/216, Ultralight…) — page write, 4 bytes per page */
    if (tag->sak == 0x00) {
        size_t pages = (ndef_len + 3) / 4;
        for (size_t p = 0; p < pages; p++) {
            uint8_t page[4] = {0};
            size_t  off = p * 4;
            size_t  n   = (ndef_len - off < 4) ? (ndef_len - off) : 4;
            memcpy(page, ndef + off, n);
            uint8_t payload[] = {0xD4, 0x40, 0x01, 0xA2, (uint8_t)(4 + p),
                                  page[0], page[1], page[2], page[3]};
            uint8_t frame[32], resp[12] = {0};
            send_recv(cs, frame, build_frame(frame, payload, sizeof(payload)), resp, sizeof(resp));
            int ok = 0;
            for (int j = 0; j < 10; j++)
                if (resp[j] == 0xD5 && resp[j+1] == 0x41) { ok = (resp[j+2] == 0); break; }
            if (!ok) return 0;
        }
        return 1;
    }

    /* MIFARE Classic 1K/4K gen1a clone — backdoor unlock, then raw block 4 write */
    if (tag->sak == 0x08 || tag->sak == 0x09 || tag->sak == 0x18 || tag->sak == 0x19) {
        uint8_t payload1[3] = {0xD4, 0x42, 0x40};
        uint8_t frame1[16], resp1[16] = {0};
        send_recv(cs, frame1, build_frame(frame1, payload1, sizeof(payload1)), resp1, sizeof(resp1));
        vTaskDelay(pdMS_TO_TICKS(10));

        uint8_t payload2[3] = {0xD4, 0x42, 0x43};
        uint8_t frame2[16], resp2[16] = {0};
        send_recv(cs, frame2, build_frame(frame2, payload2, sizeof(payload2)), resp2, sizeof(resp2));

        uint8_t block[16] = {0};
        size_t  n = ndef_len < 16 ? ndef_len : 16;
        memcpy(block, ndef, n);

        uint8_t wcmd[20];
        wcmd[0] = 0xD4; wcmd[1] = 0x42; wcmd[2] = 0xA0; wcmd[3] = 4;
        memcpy(wcmd + 4, block, 16);
        uint8_t frame3[40], resp3[16] = {0};
        send_recv(cs, frame3, build_frame(frame3, wcmd, sizeof(wcmd)), resp3, sizeof(resp3));

        for (int j = 0; j + 2 < 16; j++)
            if (resp3[j] == 0xD5 && resp3[j+1] == 0x43 && resp3[j+2] == 0x00) return 1;
        return 0;
    }

    return 0;  /* unknown tag type */
}

/* Read NDEF Text record from tag on cs; writes null-terminated name into name_out.
 * Returns 1 if a non-empty name was found, 0 otherwise.
 * Reads 48 bytes (NTAG213: 3×ReadPage; MIFARE Classic gen1a: 3×ReadBlock after unlock). */
static int ndef_read(int cs, const tag_t *tag, char *name_out, size_t max_name)
{
    uint8_t raw[48] = {0};

    if (tag->sak == 0x00) {
        /* ISO 14443-A Type 2 Tag (T2T): NTAG213/215/216, Ultralight, Ultralight-C, etc.
         * All use the same page-read command (0x30). Read pages 4, 8, 12 = 48 bytes. */
        static const uint8_t pages[3] = {4, 8, 12};
        for (int p = 0; p < 3; p++) {
            uint8_t payload[] = {0xD4, 0x40, 0x01, 0x30, pages[p]};
            uint8_t frame[16], resp[32] = {0};
            send_recv(cs, frame, build_frame(frame, payload, sizeof(payload)), resp, sizeof(resp));
            for (int i = 0; i < 26; i++) {
                if (resp[i] == 0xD5 && resp[i+1] == 0x41 && resp[i+2] == 0x00) {
                    memcpy(raw + p * 16, resp + i + 3, 16);
                    break;
                }
            }
        }
    } else if (tag->sak == 0x08 || tag->sak == 0x09 || tag->sak == 0x18 || tag->sak == 0x19) {
        /* MIFARE Classic (1K/4K) gen1a clone: backdoor unlock then three ReadBlock commands.
         * gen1a magic bytes work regardless of exact variant/manufacturer. */
        uint8_t p1[3] = {0xD4, 0x42, 0x40};
        uint8_t f1[16], r1[16] = {0};
        send_recv(cs, f1, build_frame(f1, p1, sizeof(p1)), r1, sizeof(r1));
        vTaskDelay(pdMS_TO_TICKS(10));
        uint8_t p2[3] = {0xD4, 0x42, 0x43};
        uint8_t f2[16], r2[16] = {0};
        send_recv(cs, f2, build_frame(f2, p2, sizeof(p2)), r2, sizeof(r2));

        static const uint8_t blocks[3] = {4, 5, 6};
        for (int b = 0; b < 3; b++) {
            uint8_t p3[4] = {0xD4, 0x42, 0x30, blocks[b]};
            uint8_t f3[16], r3[24] = {0};
            send_recv(cs, f3, build_frame(f3, p3, sizeof(p3)), r3, sizeof(r3));
            for (int i = 0; i < 20; i++) {
                if (r3[i] == 0xD5 && r3[i+1] == 0x43 && r3[i+2] == 0x00) {
                    memcpy(raw + b * 16, r3 + i + 3, 16);
                    break;
                }
            }
        }
    } else {
        return 0;
    }

    /* Parse NDEF TLV: 0x03 | rec_len | record... | 0xFE */
    if (raw[0] != 0x03) return 0;
    uint8_t rec_len = raw[1];
    if (rec_len < 7 || (size_t)(2 + rec_len) > sizeof(raw)) return 0;

    uint8_t *rec = raw + 2;
    /* flags=0xD1 type_len=0x01 payload_len type='T' status lang... text */
    if (rec[0] != 0xD1 || rec[1] != 0x01) return 0;
    uint8_t payload_len = rec[2];
    if (rec[3] != 0x54) return 0;       /* type must be "T" */
    if (payload_len < 3) return 0;      /* status + 2-char lang + ≥1 text char */

    uint8_t lang_len  = rec[4] & 0x3F;
    size_t  text_len  = payload_len - 1 - lang_len;
    if (text_len == 0 || lang_len > payload_len - 1) return 0;

    uint8_t *text = rec + 5 + lang_len;
    size_t   copy = text_len < max_name - 1 ? text_len : max_name - 1;
    memcpy(name_out, text, copy);
    name_out[copy] = '\0';
    return 1;
}

/* Scan cs for a tag whose UID matches target_uid; returns found tag or found=0. */
static tag_t find_tag_by_uid(int cs, const char *target_uid)
{
    tag_t t = scan_tag(cs);
    if (!t.found) return t;
    char uid_str[15];
    uid_to_str(&t, uid_str);
    if (strcmp(uid_str, target_uid) == 0) return t;
    t.found = 0;
    return t;
}

static uint32_t get_ms(void);  /* forward declaration — defined after factory_reset */

/* ── BLE session timer HAL ──────────────────────────────────────────────── */

typedef struct {
    TimerHandle_t       handle;
    imb_session_timer_cb_t cb;
    void               *arg;
} ble_timer_ctx_t;

static void ble_timer_fire(TimerHandle_t xTimer)
{
    ble_timer_ctx_t *t = (ble_timer_ctx_t *)pvTimerGetTimerID(xTimer);
    if (t->cb) t->cb(t->arg);
}

static void ble_timer_start(uint32_t ms, imb_session_timer_cb_t cb, void *arg, void *ctx)
{
    ble_timer_ctx_t *t = (ble_timer_ctx_t *)ctx;
    t->cb  = cb;
    t->arg = arg;
    if (!t->handle)
        t->handle = xTimerCreate("ble_tmr", pdMS_TO_TICKS(ms), pdFALSE, t, ble_timer_fire);
    else
        xTimerChangePeriod(t->handle, pdMS_TO_TICKS(ms), 0);
    xTimerStart(t->handle, 0);
}

static void ble_timer_stop(void *ctx)
{
    ble_timer_ctx_t *t = (ble_timer_ctx_t *)ctx;
    if (t->handle) xTimerStop(t->handle, 0);
}

static ble_timer_ctx_t g_hello_timer_ctx;
static ble_timer_ctx_t g_grace_timer_ctx;

/* ── BLE HAL wrappers ───────────────────────────────────────────────────── */

static int ble_hal_notify_event(const uint8_t *buf, size_t len, void *ctx)
{
    (void)ctx;
    return imb_ble_notify_event(buf, len) == ESP_OK ? 0 : -1;
}

static int ble_hal_notify_report(const uint8_t *buf, size_t len, void *ctx)
{
    (void)ctx;
    return imb_ble_notify_report(buf, len) == ESP_OK ? 0 : -1;
}

static void ble_hal_disconnect(void *ctx)
{
    (void)ctx;
    imb_ble_disconnect();
}

static void ble_hal_unbond(void *ctx)
{
    (void)ctx;
    imb_ble_unpair_current();
}

/* ── App context ────────────────────────────────────────────────────────── */

/* Pending name-tag request set by BLE task, consumed by main loop.
 * 60-second window: BLE task sets active=1; main loop polls the PN532
 * until target UID found and written, timeout, or BLE disconnect. */
typedef struct {
    volatile int active;       /* 1 = window open; 0 = idle/aborted */
    char     uid[IMB_UID_LEN];
    char     name[IMB_NAME_LEN];
    uint8_t  msg_id;
    uint32_t deadline_ms;      /* get_ms() + 60000 at request start */
    uint8_t  led_state;        /* 0=uninit 1=scanning 2=active-breathing */
} name_tag_req_t;

typedef struct {
    imb_session_t  *session;
    imb_registry_t *registry;
    uint32_t        pin_hash;
    char            box_name[IMB_NAME_LEN];
    name_tag_req_t  name_req;
} app_ctx_t;

/* File-scope so the BLE disconnect wrapper can abort an in-flight write. */
static app_ctx_t g_app;

/* Called by session after CMD_SET_PIN succeeds */
static void app_on_set_pin(void *ctx, uint32_t pin_hash, const char *box_name, uint8_t msg_id)
{
    (void)msg_id;
    app_ctx_t *app = (app_ctx_t *)ctx;
    app->pin_hash = pin_hash;
    strncpy(app->box_name, box_name, IMB_NAME_LEN - 1);
    app->box_name[IMB_NAME_LEN - 1] = '\0';
    nvs_save_identity(pin_hash, box_name);
    imb_ble_update_adv(pin_hash, IMB_MODE_FIELD_CHECK, 0, box_name);
    printf("[BLE] setup complete — pin_hash=0x%08lX  name=%s\n",
           (unsigned long)pin_hash, box_name);
}

/* Called by session after CMD_BOX_NAME succeeds */
static void app_on_box_rename(void *ctx, const char *new_name, uint8_t msg_id)
{
    (void)msg_id;
    app_ctx_t *app = (app_ctx_t *)ctx;
    strncpy(app->box_name, new_name, IMB_NAME_LEN - 1);
    app->box_name[IMB_NAME_LEN - 1] = '\0';
    nvs_save_box_name(app->pin_hash, app->box_name);
    imb_ble_update_adv(app->pin_hash, IMB_MODE_FIELD_CHECK, 0, app->box_name);
    printf("[BLE] box renamed → %s\n", app->box_name);
}

/* Called by session after CMD_MODE succeeds */
static void app_on_mode_set(void *ctx, imb_op_mode_e mode, uint8_t msg_id)
{
    (void)msg_id;
    app_ctx_t *app = (app_ctx_t *)ctx;
    imb_ble_update_adv(app->pin_hash, mode, 0, app->box_name);
    imb_ble_set_conn_params(mode == IMB_MODE_REGISTRATION
                            ? IMB_BLE_CONN_REGISTRATION
                            : IMB_BLE_CONN_FIELD_CHECK);
    if (mode == IMB_MODE_REGISTRATION)
        imb_buzzer_play(IMB_BUZZ_BLE_CONNECTED);
}

/* Called by BLE session when phone sends CMD_NAME { uid, name }.
 * Opens a 60-second write window; the main loop polls the PN532 until
 * the target UID is found and written, the window expires, or BLE drops. */
static void app_on_name_tag(void *ctx, const char *uid, const char *name, uint8_t msg_id)
{
    app_ctx_t *app = (app_ctx_t *)ctx;
    if (app->name_req.active)
        printf("[WARN] on_name_tag: overwriting in-flight request uid=%s\n", uid);
    strncpy(app->name_req.uid,  uid,  IMB_UID_LEN  - 1);
    strncpy(app->name_req.name, name, IMB_NAME_LEN - 1);
    app->name_req.uid[IMB_UID_LEN  - 1] = '\0';
    app->name_req.name[IMB_NAME_LEN - 1] = '\0';
    app->name_req.msg_id      = msg_id;
    app->name_req.deadline_ms = get_ms() + 60000;
    app->name_req.led_state   = 0;
    app->name_req.active      = 1;   /* publish last — main loop polls this */
}

/* Called when BLE client disconnects — abort any in-flight write window.
 * Per spec: tag stays anonymous, no ACK sent. */
static void app_on_ble_disconnected(void *ctx)
{
    if (g_app.name_req.active) {
        g_app.name_req.active = 0;
        imb_led_play(IMB_LED_BLE_IDLE);
        printf("[NDEF] write aborted — BLE disconnected\n");
    }
    imb_ble_session_on_disconnected(ctx);
}

static void on_scan_event(const imb_scan_event_t *e, void *ctx)
{
    app_ctx_t *app = (app_ctx_t *)ctx;
    imb_session_apply(app->session, e);

    switch (e->dir) {
    case IMB_INSERT:
        imb_buzzer_play(IMB_BUZZ_TAG_PLACED);
        imb_led_play(IMB_LED_TAG_INSERT);
        printf("[EVENT] INSERT  uid=%s  present=%u\n",
               e->uid, app->session->present_count);
        break;
    case IMB_EXTRACT:
        imb_buzzer_play(IMB_BUZZ_ITEM_REMOVED);
        imb_led_play(IMB_LED_TAG_EXTRACT);
        printf("[EVENT] EXTRACT uid=%s  present=%u\n",
               e->uid, app->session->present_count);
        break;
    case IMB_AMBIGUOUS:
        imb_buzzer_play(IMB_BUZZ_UNKNOWN_TAG);
        imb_led_play(IMB_LED_AMBIGUOUS);
        printf("[EVENT] AMBIGUOUS uid=%s\n", e->uid);
        break;
    }

    /* Push to BLE event queue (no-op if no client subscribed) */
    imb_pkt_event_tag_t pkt = {
        .msg_type  = IMB_MSG_EVENT_TAG,
        .direction = (uint8_t)e->dir,
    };
    strncpy(pkt.uid, e->uid, IMB_UID_LEN - 1);
    pkt.uid[IMB_UID_LEN - 1] = '\0';
    pkt.name[0] = '\0';

    imb_item_t item;
    if (app->registry && imb_registry_get(app->registry, e->uid, &item) == IMB_REG_OK) {
        /* Already registered — populate name so phone skips re-prompt */
        strncpy(pkt.name, item.name, IMB_NAME_LEN - 1);
    } else if (e->dir == IMB_INSERT && app->registry) {
        /* Unknown tag on INSERT — read NDEF and auto-register if it carries a name.
         * This enables items to migrate between meshes: the name travels with the tag. */
        tag_t found = find_tag_by_uid(PIN_CS0, e->uid);
        int   cs    = PIN_CS0;
        if (!found.found) { found = find_tag_by_uid(PIN_CS1, e->uid); cs = PIN_CS1; }
        if (found.found) {
            char ndef_name[IMB_NAME_LEN] = {0};
            if (ndef_read(cs, &found, ndef_name, IMB_NAME_LEN) && ndef_name[0] != '\0') {
                imb_item_t new_item;
                strncpy(new_item.uid,  e->uid,    IMB_UID_LEN  - 1);
                new_item.uid[IMB_UID_LEN - 1] = '\0';
                strncpy(new_item.name, ndef_name, IMB_NAME_LEN - 1);
                new_item.name[IMB_NAME_LEN - 1] = '\0';
                if (imb_registry_add(app->registry, &new_item) == IMB_REG_OK) {
                    strncpy(pkt.name, ndef_name, IMB_NAME_LEN - 1);
                    printf("[NDEF] auto-registered uid=%s name=%s\n", e->uid, ndef_name);
                }
            }
        }
    }
    imb_ble_session_push_event_tag(&pkt);
}

static uint32_t get_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* Erase all four IMB NVS namespaces, clear NimBLE bond store, reboot. */
static void factory_reset(void)
{
    printf("[FACTORY] erasing all IMB namespaces and bond store...\n");

    static const char *namespaces[] = {
        "imb_identity", "imb_state", "imb_local", "imb_txlog",
    };
    for (int i = 0; i < (int)(sizeof(namespaces) / sizeof(namespaces[0])); i++) {
        nvs_handle_t h;
        if (nvs_open(namespaces[i], NVS_READWRITE, &h) == ESP_OK) {
            nvs_erase_all(h);
            nvs_commit(h);
            nvs_close(h);
            printf("[FACTORY] erased namespace: %s\n", namespaces[i]);
        }
    }

    imb_ble_clear_all_bonds();
    printf("[FACTORY] bond store cleared — rebooting\n");
    vTaskDelay(pdMS_TO_TICKS(200));  /* let UART drain */
    esp_restart();
}

/* ── app_main ───────────────────────────────────────────────────────────── */

void app_main(void)
{
    printf("\n=== IMB Node — event loop ===\n");

    /* NVS init (required by NimBLE) */
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* Load persisted identity — if nothing stored yet, box is in SETUP mode */
    uint32_t stored_pin_hash = nvs_load_pin_hash();
    char     stored_box_name[IMB_NAME_LEN] = "Box1";
    if (stored_pin_hash != 0)
        nvs_load_box_name(stored_box_name, sizeof(stored_box_name));
    printf("[NVS] loaded pin_hash=0x%08lX  name=%s\n",
           (unsigned long)stored_pin_hash, stored_box_name);

    /* GPIO init */
    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL<<PIN_MOSI)|(1ULL<<PIN_SCK)|(1ULL<<PIN_CS0)|(1ULL<<PIN_CS1),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&out_cfg);
    gpio_config_t in_cfg = {
        .pin_bit_mask = (1ULL<<PIN_MISO)|(1ULL<<PIN_BOOT),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&in_cfg);
    gpio_set_level(PIN_CS0, 1);
    gpio_set_level(PIN_CS1, 1);
    vTaskDelay(pdMS_TO_TICKS(500));

    /* PN532 init */
    pn532_wakeup(PIN_CS0); pn532_init(PIN_CS0);
    pn532_wakeup(PIN_CS1); pn532_init(PIN_CS1);
    printf("[INIT] PN532 readers ready\n");

    /* Buzzer init */
    imb_buzzer_hal_t buz_hal = imb_buzzer_ledc_init();
    imb_buzzer_init(&buz_hal);
    printf("[INIT] Buzzer ready\n");

    /* LED init */
    imb_led_hal_t led_hal = imb_led_rmt_init();
    imb_led_init(&led_hal);
    printf("[INIT] LED ready\n");

    /* Registry init */
    static imb_registry_t registry;
    imb_registry_init(&registry, &g_reg_nvs_hal, IMB_REGISTRY_MAX_ITEMS);
    printf("[INIT] Registry ready — %u items loaded\n", imb_registry_count(&registry));

    /* Session + detector init */
    static imb_session_t session;
    imb_session_init(&session);

    g_app.session  = &session;
    g_app.registry = &registry;
    g_app.pin_hash = stored_pin_hash;
    strncpy(g_app.box_name, stored_box_name, IMB_NAME_LEN - 1);
    g_app.box_name[IMB_NAME_LEN - 1] = '\0';

    static imb_detector_t detector;
    imb_detector_init(&detector, 500, get_ms, on_scan_event, &g_app);
    printf("[INIT] Detector ready\n");

    /* BLE session HALs */
    static const imb_ble_session_ble_hal_t ble_hal = {
        .notify_event  = ble_hal_notify_event,
        .notify_report = ble_hal_notify_report,
        .disconnect    = ble_hal_disconnect,
        .unbond        = ble_hal_unbond,
    };
    static const imb_ble_session_app_cbs_t app_cbs = {
        .on_name_tag   = app_on_name_tag,
        .on_set_pin    = app_on_set_pin,
        .on_mode_set   = app_on_mode_set,
        .on_box_rename = app_on_box_rename,
        .ctx           = &g_app,
    };

    imb_ble_session_config_t sess_cfg = {
        .pin_hash = stored_pin_hash,
        .nvs      = &g_nvs_hal,
        .ble      = &ble_hal,
        .app      = &app_cbs,
        .hello_timer = {
            .start = ble_timer_start,
            .stop  = ble_timer_stop,
            .ctx   = &g_hello_timer_ctx,
        },
        .grace_timer = {
            .start = ble_timer_start,
            .stop  = ble_timer_stop,
            .ctx   = &g_grace_timer_ctx,
        },
    };
    imb_ble_session_init(&sess_cfg);

    /* BLE transport */
    static const imb_ble_callbacks_t ble_cbs = {
        .on_connected    = imb_ble_session_on_connected,
        .on_subscribed   = imb_ble_session_on_subscribed,
        .on_cmd          = imb_ble_session_on_cmd,
        .on_disconnected = app_on_ble_disconnected,
    };
    imb_ble_init(&ble_cbs, NULL);
    /* Adv reflects actual persisted state — SETUP if no PIN, named mode otherwise */
    imb_op_mode_e boot_mode = (stored_pin_hash == 0) ? IMB_MODE_SETUP : IMB_MODE_FIELD_CHECK;
    imb_op_mode_e stored_mode;
    if (stored_pin_hash != 0 && hal_nvs_read_op_mode(&stored_mode, NULL) == 0)
        boot_mode = stored_mode;
    imb_ble_update_adv(stored_pin_hash, boot_mode, 0, stored_box_name);
    printf("[INIT] BLE advertising — pin_hash=0x%08lX  mode=%d  name=%s\n\n",
           (unsigned long)stored_pin_hash, (int)boot_mode, stored_box_name);

    /* Main poll loop */
    char     uid[15];
    uint32_t boot_hold_start = 0;
    int      boot_held       = 0;
    while (1) {
        /* Factory reset: detect 10 s BOOT button hold (GPIO 0, active-low) */
        if (gpio_get_level(PIN_BOOT) == 0) {
            if (!boot_held) {
                boot_held       = 1;
                boot_hold_start = get_ms();
                imb_led_play(IMB_LED_FACTORY_HOLD);
                imb_buzzer_play(IMB_BUZZ_FACTORY_RESET);
                printf("[BOOT] button held — release within 10 s to cancel\n");
            } else if ((get_ms() - boot_hold_start) >= FACTORY_RESET_HOLD_MS) {
                imb_led_play(IMB_LED_FACTORY_RESET);
                imb_buzzer_silence();
                vTaskDelay(pdMS_TO_TICKS(500));
                factory_reset();  /* does not return */
            }
        } else {
            if (boot_held) {
                boot_held = 0;
                imb_buzzer_silence();
                imb_led_play(IMB_LED_BLE_IDLE);
                printf("[BOOT] button released — factory reset cancelled\n");
            }
        }

        /* Scan both readers */
        tag_t t0 = scan_tag(PIN_CS0);
        tag_t t1 = scan_tag(PIN_CS1);

        /* Feed results to detector for normal insert/extract tracking */
        if (t0.found) { uid_to_str(&t0, uid); imb_detector_on_reader_event(&detector, 0, uid); }
        if (t1.found) { uid_to_str(&t1, uid); imb_detector_on_reader_event(&detector, 1, uid); }
        imb_detector_tick(&detector);

        /* Async NDEF write window (60 s): opened by CMD_NAME, runs across loop ticks.
         * Only write if scanned UID matches the requested UID.
         * Abort silently on BLE disconnect (active cleared by app_on_ble_disconnected). */
        if (g_app.name_req.active) {
            uint32_t now = get_ms();

            if ((int32_t)(now - g_app.name_req.deadline_ms) >= 0) {
                /* Window expired */
                uint8_t mid = g_app.name_req.msg_id;
                g_app.name_req.active = 0;
                imb_ble_session_ack(mid, IMB_ACK_NDEF_WRITE_FAILED);
                imb_led_play(IMB_LED_BLE_IDLE);
                printf("[NDEF] 60s window expired — uid=%s\n", g_app.name_req.uid);
            } else {
                /* Find target UID in this tick's scan results */
                int   target_found = 0;
                int   target_cs    = -1;
                tag_t target_tag   = {0};
                char  uid_str[15];

                if (t0.found) {
                    uid_to_str(&t0, uid_str);
                    if (strcmp(uid_str, g_app.name_req.uid) == 0) {
                        target_found = 1; target_cs = PIN_CS0; target_tag = t0;
                    }
                }
                if (!target_found && t1.found) {
                    uid_to_str(&t1, uid_str);
                    if (strcmp(uid_str, g_app.name_req.uid) == 0) {
                        target_found = 1; target_cs = PIN_CS1; target_tag = t1;
                    }
                }

                /* LED feedback: breathing teal when tag present, solid teal when scanning */
                uint8_t want_led = target_found ? 2 : 1;
                if (g_app.name_req.led_state != want_led) {
                    g_app.name_req.led_state = want_led;
                    imb_led_play(want_led == 2 ? IMB_LED_WRITE_ACTIVE : IMB_LED_WRITE_SCANNING);
                }

                if (target_found) {
                    if (ndef_write(target_cs, &target_tag, g_app.name_req.name)) {
                        imb_item_t item;
                        strncpy(item.uid,  g_app.name_req.uid,  IMB_UID_LEN  - 1);
                        item.uid[IMB_UID_LEN  - 1] = '\0';
                        strncpy(item.name, g_app.name_req.name, IMB_NAME_LEN - 1);
                        item.name[IMB_NAME_LEN - 1] = '\0';

                        imb_ack_status_e ack_status = IMB_ACK_OK;
                        imb_reg_err_e    reg_err    = imb_registry_add(g_app.registry, &item);
                        if (reg_err == IMB_REG_ERR_FULL)  ack_status = IMB_ACK_REGISTRY_FULL;
                        else if (reg_err != IMB_REG_OK)   ack_status = IMB_ACK_NDEF_WRITE_FAILED;

                        uint8_t mid = g_app.name_req.msg_id;
                        g_app.name_req.active = 0;
                        imb_buzzer_play(IMB_BUZZ_TAG_WRITTEN);
                        imb_led_play(IMB_LED_REG_PASS);
                        imb_ble_session_ack(mid, ack_status);
                        printf("[NDEF] registered uid=%s name=%s status=%d\n",
                               item.uid, item.name, (int)ack_status);
                    }
                    /* Write failed — tag may have moved; keep window open and retry */
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
