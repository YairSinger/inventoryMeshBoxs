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

    /* NTAG213 — page write, 4 bytes per page starting at page 4 */
    if (tag->atqa[0] == 0x44 && tag->atqa[1] == 0x00 && tag->sak == 0x00) {
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

    /* MIFARE Classic — gen1a backdoor unlock, then raw block 4 write */
    if (tag->atqa[0] == 0x00 && tag->atqa[1] == 0x04 && tag->sak == 0x08) {
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

/* Pending name-tag request set by BLE task, consumed by main loop */
typedef struct {
    volatile int active;       /* 1 = request pending */
    char uid[IMB_UID_LEN];
    char name[IMB_NAME_LEN];
    uint8_t msg_id;
} name_tag_req_t;

typedef struct {
    imb_session_t  *session;
    imb_registry_t *registry;
    uint32_t        pin_hash;
    char            box_name[IMB_NAME_LEN];
    name_tag_req_t  name_req;
} app_ctx_t;

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
 * Stores the request; NDEF write + registry update happen in the main loop
 * to avoid concurrent SPI access from the NimBLE task. */
static void app_on_name_tag(void *ctx, const char *uid, const char *name, uint8_t msg_id)
{
    app_ctx_t *app = (app_ctx_t *)ctx;
    if (app->name_req.active) {
        /* Previous request still in-flight — shouldn't happen, but drop it */
        printf("[WARN] on_name_tag: overwriting in-flight request uid=%s\n", uid);
    }
    strncpy(app->name_req.uid,  uid,  IMB_UID_LEN  - 1);
    strncpy(app->name_req.name, name, IMB_NAME_LEN - 1);
    app->name_req.uid[IMB_UID_LEN  - 1] = '\0';
    app->name_req.name[IMB_NAME_LEN - 1] = '\0';
    app->name_req.msg_id = msg_id;
    app->name_req.active = 1;   /* publish last — main loop polls this */
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
    /* Populate name so phone skips re-prompt for already-registered tags */
    imb_item_t item;
    if (app->registry && imb_registry_get(app->registry, e->uid, &item) == IMB_REG_OK)
        strncpy(pkt.name, item.name, IMB_NAME_LEN - 1);
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

    static app_ctx_t app_ctx;
    app_ctx.session  = &session;
    app_ctx.registry = &registry;
    app_ctx.pin_hash = stored_pin_hash;
    strncpy(app_ctx.box_name, stored_box_name, IMB_NAME_LEN - 1);
    app_ctx.box_name[IMB_NAME_LEN - 1] = '\0';

    static imb_detector_t detector;
    imb_detector_init(&detector, 500, get_ms, on_scan_event, &app_ctx);
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
        .ctx           = &app_ctx,
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
        .on_disconnected = imb_ble_session_on_disconnected,
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
    char uid[15];
    uint32_t boot_hold_start = 0;
    int      boot_held       = 0;
    while (1) {
        /* Factory reset: detect 10 s BOOT button hold (GPIO 0, active-low) */
        if (gpio_get_level(PIN_BOOT) == 0) {
            if (!boot_held) {
                boot_held        = 1;
                boot_hold_start  = get_ms();
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
        /* Handle pending name-tag NDEF write (set by BLE task via app_on_name_tag) */
        if (app_ctx.name_req.active) {
            app_ctx.name_req.active = 0;
            const char *req_uid  = app_ctx.name_req.uid;
            const char *req_name = app_ctx.name_req.name;
            uint8_t     req_mid  = app_ctx.name_req.msg_id;

            printf("[NDEF] writing '%s' to tag uid=%s\n", req_name, req_uid);

            tag_t tag    = find_tag_by_uid(PIN_CS0, req_uid);
            int   tag_cs = PIN_CS0;
            if (!tag.found) { tag = find_tag_by_uid(PIN_CS1, req_uid); tag_cs = PIN_CS1; }

            imb_ack_status_e ack_status = IMB_ACK_NDEF_WRITE_FAILED;
            if (tag.found && ndef_write(tag_cs, &tag, req_name)) {
                imb_item_t item;
                strncpy(item.uid,  req_uid,  IMB_UID_LEN  - 1); item.uid[IMB_UID_LEN  - 1] = '\0';
                strncpy(item.name, req_name, IMB_NAME_LEN - 1); item.name[IMB_NAME_LEN - 1] = '\0';
                imb_reg_err_e reg_err = imb_registry_add(app_ctx.registry, &item);
                if (reg_err == IMB_REG_OK) {
                    ack_status = IMB_ACK_OK;
                    printf("[NDEF] registered uid=%s name=%s\n", req_uid, req_name);
                } else if (reg_err == IMB_REG_ERR_FULL) {
                    ack_status = IMB_ACK_REGISTRY_FULL;
                    printf("[NDEF] registry full — uid=%s\n", req_uid);
                } else {
                    printf("[NDEF] registry add failed err=%d uid=%s\n", reg_err, req_uid);
                }
            } else {
                printf("[NDEF] write failed — tag %s on reader\n",
                       tag.found ? "found but write failed" : "not found");
            }
            imb_ble_session_ack(req_mid, ack_status);
        }

        tag_t t0 = scan_tag(PIN_CS0);
        if (t0.found) {
            uid_to_str(&t0, uid);
            imb_detector_on_reader_event(&detector, 0, uid);
        }

        tag_t t1 = scan_tag(PIN_CS1);
        if (t1.found) {
            uid_to_str(&t1, uid);
            imb_detector_on_reader_event(&detector, 1, uid);
        }

        imb_detector_tick(&detector);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
