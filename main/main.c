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
#include "imb_ble.h"
#include "imb_ble_session.h"

/* ── Pin map ────────────────────────────────────────────────────────────── */

#define PIN_MOSI  11
#define PIN_MISO  13
#define PIN_SCK   12
#define PIN_CS0   10   /* inner reader (reader 0) */
#define PIN_CS1    9   /* outer reader (reader 1) */

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

typedef struct { uint8_t uid[10]; uint8_t uid_len; int found; } tag_t;

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
            t.uid_len = resp[i+7]; if (t.uid_len > 10) t.uid_len = 10;
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

typedef struct {
    imb_session_t *session;
    uint32_t       pin_hash;
    char           box_name[IMB_NAME_LEN];
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
    strncpy(pkt.uid,  e->uid,  IMB_UID_LEN  - 1);
    pkt.name[0] = '\0';
    imb_ble_session_push_event_tag(&pkt);
}

static uint32_t get_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
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
        .pin_bit_mask = (1ULL<<PIN_MISO),
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

    /* Session + detector init */
    static imb_session_t session;
    imb_session_init(&session);

    static app_ctx_t app_ctx;
    app_ctx.session  = &session;
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
    while (1) {
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
