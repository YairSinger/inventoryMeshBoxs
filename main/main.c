/* IMB Node — main application loop
 *
 * Stack: PN532 (bit-bang SPI) → imb_detector → imb_session + imb_buzzer
 *
 * Wiring (hardware.md):
 *   MOSI=11, MISO=13, SCK=12
 *   CS inner (reader 0) = GPIO10
 *   CS outer (reader 1) = GPIO9
 *   Buzzer = GPIO17 (LEDC PWM, direct drive)
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"
#include "esp_timer.h"
#include "imb_buzzer.h"
#include "imb_buzzer_ledc.h"
#include "imb_detector.h"
#include "imb_session.h"

/* ── Pin map ────────────────────────────────────────────────────────────── */

#define PIN_MOSI  11
#define PIN_MISO  13
#define PIN_SCK   12
#define PIN_CS0   10   /* inner reader (reader 0) */
#define PIN_CS1    9   /* outer reader (reader 1) */

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

/* ── App context ────────────────────────────────────────────────────────── */

typedef struct {
    imb_session_t *session;
} app_ctx_t;

static void on_scan_event(const imb_scan_event_t *e, void *ctx)
{
    app_ctx_t *app = (app_ctx_t *)ctx;
    imb_session_apply(app->session, e);

    switch (e->dir) {
    case IMB_INSERT:
        imb_buzzer_play(IMB_BUZZ_TAG_PLACED);
        printf("[EVENT] INSERT  uid=%s  present=%u\n",
               e->uid, app->session->present_count);
        break;
    case IMB_EXTRACT:
        imb_buzzer_play(IMB_BUZZ_ITEM_REMOVED);
        printf("[EVENT] EXTRACT uid=%s  present=%u\n",
               e->uid, app->session->present_count);
        break;
    case IMB_AMBIGUOUS:
        imb_buzzer_play(IMB_BUZZ_UNKNOWN_TAG);
        printf("[EVENT] AMBIGUOUS uid=%s\n", e->uid);
        break;
    }
}

static uint32_t get_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ── app_main ───────────────────────────────────────────────────────────── */

void app_main(void)
{
    printf("\n=== IMB Node — event loop ===\n");

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

    /* Session + detector init */
    static imb_session_t session;
    imb_session_init(&session);

    static app_ctx_t app_ctx;
    app_ctx.session = &session;

    static imb_detector_t detector;
    imb_detector_init(&detector, 500, get_ms, on_scan_event, &app_ctx);
    printf("[INIT] Detector ready — scanning...\n\n");

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
