/* IMB On-Device Driver Integration Tests
 * Standalone ESP-IDF project — NOT production firmware.
 *
 * Tests run on every boot in order:
 *   1. NVS      — write / read / erase in imb_local namespace
 *   2. tag_write — MIFARE Classic 1K (auth + block write/read) or NTAG213 (NDEF pages)
 *   3. deep_sleep — 3 s timer wakeup, verifies wakeup cause
 *
 * Output tokens parsed by tools/integration_tests/harness.py:
 *   [PASS] <name>
 *   [FAIL] <name>: <reason>
 *   [SKIP] <name>: <reason>
 *   [DONE]
 *
 * Build + flash (from this directory):
 *   idf.py set-target esp32s3
 *   idf.py build && idf.py -p /dev/cu.usbserial-* flash
 *
 * Run tests:
 *   python3 tools/integration_tests/run_all.py
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_timer.h"
#include "imb_buzzer.h"
#include "imb_buzzer_ledc.h"
#include "imb_detector.h"

/* ------------------------------------------------------------------ */
/* Pin config (hardware.md)                                            */
/* ------------------------------------------------------------------ */
#define PIN_MOSI  11
#define PIN_MISO  13
#define PIN_SCK   12
#define PIN_CS1   10   /* inner reader */
#define PIN_CS2    9   /* outer reader */

#define PN532_SPI_STATREAD  0x02
#define PN532_SPI_DATAWRITE 0x01
#define PN532_SPI_DATAREAD  0x03

/* Survives deep sleep in RTC-fast memory */
static RTC_DATA_ATTR int s_boot_count = 0;

/* ------------------------------------------------------------------ */
/* Pre-built PN532 frames                                              */
/* ------------------------------------------------------------------ */
static const uint8_t cmd_get_fw[] = {
    0x00, 0x00, 0xFF, 0x02, 0xFE, 0xD4, 0x02, 0x2A, 0x00
};
static const uint8_t cmd_sam_config[] = {
    0x00, 0x00, 0xFF, 0x05, 0xFB,
    0xD4, 0x14, 0x01, 0x00, 0x00,
    0x17, 0x00
};
static const uint8_t cmd_rfconfig_retries[] = {
    0x00, 0x00, 0xFF, 0x06, 0xFA,
    0xD4, 0x32, 0x05, 0xFF, 0xFF, 0x03,
    0xF4, 0x00
};
static const uint8_t cmd_14443a[] = {
    0x00, 0x00, 0xFF, 0x04, 0xFC,
    0xD4, 0x4A, 0x01, 0x00,
    0xE1, 0x00
};

/* ------------------------------------------------------------------ */
/* Bit-bang SPI (LSB-first, mode 0)                                    */
/* ------------------------------------------------------------------ */
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

static uint8_t bb_read_ex(int cs, uint8_t cmd, uint8_t *rx, size_t len)
{
    gpio_set_level(cs, 0); ets_delay_us(5);
    uint8_t first = bb_byte(cmd);
    for (size_t i = 0; i < len; i++) rx[i] = bb_byte(0x00);
    ets_delay_us(5); gpio_set_level(cs, 1);
    return first;
}

static void bb_read(int cs, uint8_t cmd, uint8_t *rx, size_t len)
{
    bb_read_ex(cs, cmd, rx, len);
}

static int wait_ready(int cs, int tries, int delay_ms)
{
    for (int i = 0; i < tries; i++) {
        uint8_t s = 0;
        bb_read(cs, PN532_SPI_STATREAD, &s, 1);
        if (s == 0x01) return 1;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    return 0;
}

static void wakeup(int cs)
{
    gpio_set_level(cs, 0); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(cs, 1); vTaskDelay(pdMS_TO_TICKS(5));
}

static void send_recv(int cs, const uint8_t *frame, size_t flen,
                      uint8_t *resp, size_t rlen)
{
    uint8_t wbuf[64];
    wbuf[0] = PN532_SPI_DATAWRITE;
    memcpy(wbuf + 1, frame, flen);
    bb_write(cs, wbuf, flen + 1);
    if (!wait_ready(cs, 50, 10)) return;
    uint8_t ack[6] = {0}; bb_read(cs, PN532_SPI_DATAREAD, ack, 6);
    if (!wait_ready(cs, 50, 10)) return;
    bb_read(cs, PN532_SPI_DATAREAD, resp, rlen);
}

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

/* ------------------------------------------------------------------ */
/* PN532 bringup                                                       */
/* ------------------------------------------------------------------ */
static int pn532_probe(int cs)
{
    wakeup(cs);
    uint8_t wbuf[sizeof(cmd_get_fw) + 1];
    wbuf[0] = PN532_SPI_DATAWRITE;
    memcpy(wbuf + 1, cmd_get_fw, sizeof(cmd_get_fw));
    bb_write(cs, wbuf, sizeof(wbuf));

    int ready = 0;
    for (int i = 0; i < 100 && !ready; i++) {
        uint8_t op = 0, st = 0;
        op = bb_read_ex(cs, PN532_SPI_STATREAD, &st, 1);
        if (st == 0x01) { ready = 1; }
        else if (op == 0x01) { uint8_t t[5]; bb_read(cs, PN532_SPI_DATAREAD, t, 5); ready = 2; }
        else { vTaskDelay(pdMS_TO_TICKS(10)); }
    }
    if (ready == 1) { uint8_t a[6]; bb_read(cs, PN532_SPI_DATAREAD, a, 6); }
    if (!wait_ready(cs, 100, 5)) return 0;
    uint8_t resp[20] = {0};
    bb_read(cs, PN532_SPI_DATAREAD, resp, 20);
    for (int i = 0; i < 18; i++)
        if (resp[i] == 0xD5 && resp[i+1] == 0x03) return 1;
    return 0;
}

static void pn532_init(int cs)
{
    uint8_t r[8] = {0};
    send_recv(cs, cmd_sam_config, sizeof(cmd_sam_config), r, sizeof(r));
    send_recv(cs, cmd_rfconfig_retries, sizeof(cmd_rfconfig_retries), r, sizeof(r));
}

/* ------------------------------------------------------------------ */
/* Tag scan                                                            */
/* ------------------------------------------------------------------ */
typedef struct {
    uint8_t atqa[2], sak, uid[10], uid_len;
    int     found;
} tag_t;

static tag_t scan_tag(int cs)
{
    tag_t t = {0};
    wakeup(cs);
    uint8_t wbuf[sizeof(cmd_14443a) + 1];
    wbuf[0] = PN532_SPI_DATAWRITE;
    memcpy(wbuf + 1, cmd_14443a, sizeof(cmd_14443a));
    bb_write(cs, wbuf, sizeof(wbuf));

    if (!wait_ready(cs, 50, 10)) return t;
    uint8_t ack[6] = {0}; bb_read(cs, PN532_SPI_DATAREAD, ack, 6);
    if (!wait_ready(cs, 50, 10)) return t;
    uint8_t resp[32] = {0};
    bb_read(cs, PN532_SPI_DATAREAD, resp, sizeof(resp));

    for (int i = 0; i < 28; i++) {
        if (resp[i] == 0xD5 && resp[i+1] == 0x4B && resp[i+2] > 0) {
            t.atqa[0] = resp[i+4]; t.atqa[1] = resp[i+5];
            t.sak = resp[i+6];
            t.uid_len = resp[i+7]; if (t.uid_len > 10) t.uid_len = 10;
            memcpy(t.uid, resp + i + 8, t.uid_len);
            t.found = 1;
            return t;
        }
    }
    return t;
}

/* ------------------------------------------------------------------ */
/* MIFARE Classic helpers                                              */
/* ------------------------------------------------------------------ */

/* Low-level auth: key_type = 0x60 (Key A) or 0x61 (Key B).
 * After a failed auth the PN532 drops the tag to IDLE — caller must
 * re-select with scan_tag() before the next attempt. */
static int mifare_auth(int cs, uint8_t block_num, const uint8_t uid[4],
                       uint8_t key_type, const uint8_t key[6])
{
    uint8_t payload[15] = {
        0xD4, 0x40, 0x01,
        key_type, block_num,
        key[0], key[1], key[2], key[3], key[4], key[5],
        uid[0], uid[1], uid[2], uid[3],
    };
    uint8_t frame[32], resp[12] = {0};
    send_recv(cs, frame, build_frame(frame, payload, sizeof(payload)), resp, sizeof(resp));
    printf("[DBG] mifare_auth resp:");
    for (int j = 0; j < 12; j++) printf(" %02X", resp[j]);
    printf("\n");
    for (int i = 0; i < 10; i++)
        if (resp[i] == 0xD5 && resp[i+1] == 0x41)
            return (resp[i+2] == 0x00) ? 1 : 0;
    return 0;
}

/* Write 16 bytes to block_num (sector must already be authenticated). */
static int mifare_write_block(int cs, uint8_t block_num, const uint8_t d[16])
{
    uint8_t payload[21] = {0xD4, 0x40, 0x01, 0xA0, block_num};
    memcpy(payload + 5, d, 16);
    uint8_t frame[40], resp[12] = {0};
    send_recv(cs, frame, build_frame(frame, payload, sizeof(payload)), resp, sizeof(resp));
    for (int i = 0; i < 10; i++)
        if (resp[i] == 0xD5 && resp[i+1] == 0x41)
            return (resp[i+2] == 0x00) ? 1 : 0;
    return 0;
}

/* Read 16 bytes from block_num (sector must already be authenticated). */
static int mifare_read_block(int cs, uint8_t block_num, uint8_t out[16])
{
    uint8_t payload[] = {0xD4, 0x40, 0x01, 0x30, block_num};
    uint8_t frame[32], resp[24] = {0};
    send_recv(cs, frame, build_frame(frame, payload, sizeof(payload)), resp, sizeof(resp));
    for (int i = 0; i < 8; i++) {
        if (resp[i] == 0xD5 && resp[i+1] == 0x41 && resp[i+2] == 0x00) {
            memcpy(out, resp + i + 3, 16);
            return 1;
        }
    }
    return 0;
}

/* Probe common keys for a block. Re-selects tag before each attempt
 * because a failed auth drops it back to IDLE.
 * Returns 1 and fills working_key_type/key on success, 0 on failure. */
typedef struct { uint8_t type; uint8_t key[6]; const char *label; } mifare_key_t;

static const mifare_key_t PROBE_KEYS[] = {
    {0x60, {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}, "Key A FF*6 (factory default)"},
    {0x61, {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}, "Key B FF*6"},
    {0x60, {0xA0,0xA1,0xA2,0xA3,0xA4,0xA5}, "Key A A0..A5 (NXP transport)"},
    {0x60, {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7}, "Key A D3F7 (MAD sector)"},
    {0x60, {0x00,0x00,0x00,0x00,0x00,0x00}, "Key A 00*6"},
};
#define N_PROBE_KEYS ((int)(sizeof(PROBE_KEYS)/sizeof(PROBE_KEYS[0])))

static int mifare_probe_auth(int cs, uint8_t block_num, tag_t *tag,
                             uint8_t *out_type, uint8_t out_key[6])
{
    for (int k = 0; k < N_PROBE_KEYS; k++) {
        printf("[INFO] tag_write: trying %s...\n", PROBE_KEYS[k].label);

        /* Re-select tag — required after any failed auth */
        *tag = scan_tag(cs);
        if (!tag->found) { printf("[INFO] tag_write: tag lost during key probe\n"); return 0; }

        if (mifare_auth(cs, block_num, tag->uid,
                        PROBE_KEYS[k].type, PROBE_KEYS[k].key)) {
            printf("[INFO] tag_write: auth OK with %s\n", PROBE_KEYS[k].label);
            *out_type = PROBE_KEYS[k].type;
            memcpy(out_key, PROBE_KEYS[k].key, 6);
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* InCommunicateThru (D4 42) — raw passthrough to activated tag       */
/* ------------------------------------------------------------------ */

/* Send raw bytes to the tag bypassing PN532 MIFARE state machine.
 * resp[] receives the full PN532 frame; returns 1 if D5 43 00 seen. */
static int incommthru(int cs, const uint8_t *cmd, size_t clen,
                      uint8_t *resp, size_t rlen)
{
    uint8_t payload[64];
    if (clen + 2 > sizeof(payload)) return 0;
    payload[0] = 0xD4;
    payload[1] = 0x42;   /* InCommunicateThru */
    memcpy(payload + 2, cmd, clen);

    uint8_t frame[80];
    send_recv(cs, frame, build_frame(frame, payload, 2 + clen), resp, rlen);

    printf("[DBG] incommthru resp:");
    for (size_t j = 0; j < (rlen < 16 ? rlen : 16); j++) printf(" %02X", resp[j]);
    printf("\n");

    for (size_t i = 0; i + 2 < rlen; i++)
        if (resp[i] == 0xD5 && resp[i+1] == 0x43)
            return (resp[i+2] == 0x00) ? 1 : 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Gen1a "magic" MIFARE Classic backdoor                              */
/* ------------------------------------------------------------------ */

/* Two-phase backdoor unlock via InCommunicateThru (raw pass-through).
 * InDataExchange (D4 40) returns Application Error 0x7F 0x81 on this
 * hardware; InCommunicateThru (D4 42) bypasses the PN532 state machine. */
static int gen1a_unlock(int cs)
{
    uint8_t resp[16];

    /* Phase 1: 0x40 magic wakeup */
    uint8_t p1[] = {0x40};
    memset(resp, 0, sizeof(resp));
    int ok1 = incommthru(cs, p1, sizeof(p1), resp, sizeof(resp));
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Phase 2: 0x43 unlock */
    uint8_t p2[] = {0x43};
    memset(resp, 0, sizeof(resp));
    int ok2 = incommthru(cs, p2, sizeof(p2), resp, sizeof(resp));

    return ok1 || ok2;   /* accept if either phase acked */
}

/* Write + readback block 4 after gen1a_unlock() — no auth, raw commands. */
static void tag_write_gen1a(int cs)
{
    static const uint8_t BLOCK = 4;
    static const uint8_t DATA[16] = "IMB-DRIVER-TEST!";

    /* Write: MIFARE WRITE (0xA0) block_num + 16 bytes data */
    uint8_t wcmd[18] = {0xA0, BLOCK};
    memcpy(wcmd + 2, DATA, 16);
    uint8_t wresp[16] = {0};
    if (!incommthru(cs, wcmd, sizeof(wcmd), wresp, sizeof(wresp))) {
        printf("[FAIL] tag_write: Gen1a write failed (block %d)\n", BLOCK);
        return;
    }

    /* Read back: MIFARE READ (0x30) block_num */
    uint8_t rcmd[] = {0x30, BLOCK};
    uint8_t rresp[24] = {0};
    if (!incommthru(cs, rcmd, sizeof(rcmd), rresp, sizeof(rresp))) {
        printf("[FAIL] tag_write: Gen1a readback failed\n");
        return;
    }

    /* Card data starts at D5 43 00 + card_bytes; find them in rresp */
    uint8_t rb[16] = {0};
    for (int i = 0; i + 18 < (int)sizeof(rresp); i++) {
        if (rresp[i] == 0xD5 && rresp[i+1] == 0x43 && rresp[i+2] == 0x00) {
            memcpy(rb, rresp + i + 3, 16);
            break;
        }
    }

    if (memcmp(rb, DATA, 16) != 0) {
        printf("[FAIL] tag_write: Gen1a readback mismatch\n");
        printf("[INFO] got:      ");
        for (int i = 0; i < 16; i++) printf("%02X ", rb[i]);
        printf("\n[INFO] expected: ");
        for (int i = 0; i < 16; i++) printf("%02X ", DATA[i]);
        printf("\n");
        return;
    }
    printf("[PASS] tag_write\n");
}

/* ------------------------------------------------------------------ */
/* NTAG213 helpers                                                     */
/* ------------------------------------------------------------------ */
static int ntag_write_page(int cs, uint8_t page, const uint8_t d[4])
{
    uint8_t payload[] = {0xD4, 0x40, 0x01, 0xA2, page, d[0], d[1], d[2], d[3]};
    uint8_t frame[32], resp[12] = {0};
    send_recv(cs, frame, build_frame(frame, payload, sizeof(payload)), resp, sizeof(resp));
    for (int i = 0; i < 10; i++)
        if (resp[i] == 0xD5 && resp[i+1] == 0x41)
            return (resp[i+2] == 0x00) ? 1 : 0;
    return 0;
}

static int ntag_read_pages(int cs, uint8_t start_page, uint8_t out[16])
{
    uint8_t payload[] = {0xD4, 0x40, 0x01, 0x30, start_page};
    uint8_t frame[32], resp[24] = {0};
    send_recv(cs, frame, build_frame(frame, payload, sizeof(payload)), resp, sizeof(resp));
    for (int i = 0; i < 8; i++) {
        if (resp[i] == 0xD5 && resp[i+1] == 0x41 && resp[i+2] == 0x00) {
            memcpy(out, resp + i + 3, 16);
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* TEST 0 — Buzzer: LEDC init + all six patterns complete             */
/* ------------------------------------------------------------------ */

#define BUZZER_WAIT_MS(ms) vTaskDelay(pdMS_TO_TICKS(ms))

static void test_buzzer(void)
{
    imb_buzzer_hal_t hal = imb_buzzer_ledc_init();
    imb_buzzer_init(&hal);
    printf("[PASS] buzzer_init\n");

    /* Each wait is pattern total duration + 150 ms margin */
    imb_buzzer_play(IMB_BUZZ_TAG_PLACED);
    BUZZER_WAIT_MS(200);   /* 50 ms pattern */
    printf(imb_buzzer_is_idle() ? "[PASS] buzzer_tag_placed\n"
                                : "[FAIL] buzzer_tag_placed: not idle\n");

    imb_buzzer_play(IMB_BUZZ_ITEM_REMOVED);
    BUZZER_WAIT_MS(350);   /* 50+50+50 ms pattern */
    printf(imb_buzzer_is_idle() ? "[PASS] buzzer_item_removed\n"
                                : "[FAIL] buzzer_item_removed: not idle\n");

    imb_buzzer_play(IMB_BUZZ_UNKNOWN_TAG);
    BUZZER_WAIT_MS(500);   /* 300 ms pattern */
    printf(imb_buzzer_is_idle() ? "[PASS] buzzer_unknown_tag\n"
                                : "[FAIL] buzzer_unknown_tag: not idle\n");

    imb_buzzer_play(IMB_BUZZ_ERROR);
    BUZZER_WAIT_MS(600);   /* 80+40+80+40+80 = 320 ms pattern */
    printf(imb_buzzer_is_idle() ? "[PASS] buzzer_error\n"
                                : "[FAIL] buzzer_error: not idle\n");

    imb_buzzer_play(IMB_BUZZ_BLE_CONNECTED);
    BUZZER_WAIT_MS(400);   /* 80+30+80 = 190 ms pattern */
    printf(imb_buzzer_is_idle() ? "[PASS] buzzer_ble_connected\n"
                                : "[FAIL] buzzer_ble_connected: not idle\n");

    imb_buzzer_play(IMB_BUZZ_FACTORY_RESET);
    BUZZER_WAIT_MS(100);   /* continuous — must NOT be idle yet */
    if (imb_buzzer_is_idle()) {
        printf("[FAIL] buzzer_factory_reset: went idle before silence\n");
    } else {
        imb_buzzer_silence();
        printf(imb_buzzer_is_idle() ? "[PASS] buzzer_factory_reset\n"
                                    : "[FAIL] buzzer_factory_reset: not idle after silence\n");
    }
}

/* ------------------------------------------------------------------ */
/* TEST — Buzzer event wiring: INSERT → TAG_PLACED, EXTRACT → ITEM_REMOVED
 *
 * Polls both PN532 readers for 30 s. Pass a tag through the opening
 * (inner reader first = INSERT, outer first = EXTRACT).
 * Buzzer fires the matching pattern; [PASS] printed once per event type.
 * ------------------------------------------------------------------ */

static uint32_t get_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void uid_to_str(const tag_t *t, char out[15])
{
    int n = t->uid_len > 7 ? 7 : t->uid_len;
    int i;
    for (i = 0; i < n; i++)
        snprintf(out + i * 2, 3, "%02X", t->uid[i]);
    out[i * 2] = '\0';
}

#define TARGET_COUNT 5

typedef struct {
    int insert_count;
    int extract_count;
} event_flags_t;

static void on_detector_event(const imb_scan_event_t *e, void *ctx)
{
    event_flags_t *f = (event_flags_t *)ctx;
    if (e->dir == IMB_INSERT && f->insert_count < TARGET_COUNT) {
        f->insert_count++;
        imb_buzzer_play(IMB_BUZZ_TAG_PLACED);
        printf("[INFO] buzzer_events: INSERT %d/%d — uid=%s\n",
               f->insert_count, TARGET_COUNT, e->uid);
        if (f->insert_count == TARGET_COUNT)
            printf("[PASS] buzzer_on_insert\n");
    } else if (e->dir == IMB_EXTRACT && f->extract_count < TARGET_COUNT) {
        f->extract_count++;
        imb_buzzer_play(IMB_BUZZ_ITEM_REMOVED);
        printf("[INFO] buzzer_events: EXTRACT %d/%d — uid=%s\n",
               f->extract_count, TARGET_COUNT, e->uid);
        if (f->extract_count == TARGET_COUNT)
            printf("[PASS] buzzer_on_extract\n");
    } else if (e->dir == IMB_AMBIGUOUS) {
        printf("[INFO] buzzer_events: AMBIGUOUS — uid=%s\n", e->uid);
    }
}

static void test_buzzer_events(void)
{
    printf("\n");
    printf("[INFO] buzzer_events: ============================================\n");
    printf("[INFO] buzzer_events: MANUAL STEP — %dx INSERT + %dx EXTRACT, 90 s\n",
           TARGET_COUNT, TARGET_COUNT);
    printf("[INFO] buzzer_events: \n");
    printf("[INFO] buzzer_events:   INSERT:  pass tag INNER reader first (GPIO%d)\n", PIN_CS1);
    printf("[INFO] buzzer_events:            then OUTER reader (GPIO%d)\n", PIN_CS2);
    printf("[INFO] buzzer_events:            => expect 1 short beep\n");
    printf("[INFO] buzzer_events: \n");
    printf("[INFO] buzzer_events:   EXTRACT: pass tag OUTER reader first (GPIO%d)\n", PIN_CS2);
    printf("[INFO] buzzer_events:            then INNER reader (GPIO%d)\n", PIN_CS1);
    printf("[INFO] buzzer_events:            => expect 2 short beeps\n");
    printf("[INFO] buzzer_events: ============================================\n");

    event_flags_t flags = {0, 0};
    imb_detector_t det;
    imb_detector_init(&det, 500, get_ms, on_detector_event, &flags);

    uint32_t deadline = get_ms() + 90000;

    while (get_ms() < deadline &&
           (flags.insert_count < TARGET_COUNT || flags.extract_count < TARGET_COUNT)) {
        char uid[15];

        tag_t t1 = scan_tag(PIN_CS1);
        if (t1.found) {
            uid_to_str(&t1, uid);
            imb_detector_on_reader_event(&det, 0, uid);
        }

        tag_t t2 = scan_tag(PIN_CS2);
        if (t2.found) {
            uid_to_str(&t2, uid);
            imb_detector_on_reader_event(&det, 1, uid);
        }

        imb_detector_tick(&det);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (flags.insert_count  < TARGET_COUNT)
        printf("[SKIP] buzzer_on_insert: only %d/%d within timeout\n",
               flags.insert_count, TARGET_COUNT);
    if (flags.extract_count < TARGET_COUNT)
        printf("[SKIP] buzzer_on_extract: only %d/%d within timeout\n",
               flags.extract_count, TARGET_COUNT);
}

/* ------------------------------------------------------------------ */
/* TEST 1 — NVS write / read / erase                                  */
/* ------------------------------------------------------------------ */
static void test_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) { printf("[FAIL] nvs_init: %d\n", err); return; }

    nvs_handle_t h;
    if (nvs_open("imb_local", NVS_READWRITE, &h) != ESP_OK) {
        printf("[FAIL] nvs_open\n"); return;
    }

    const char *val = "imb_hello";
    nvs_set_str(h, "test_key", val);
    nvs_commit(h);

    char buf[32]; size_t len = sizeof(buf);
    err = nvs_get_str(h, "test_key", buf, &len);
    if (err != ESP_OK || strcmp(buf, val) != 0) {
        printf("[FAIL] nvs_write_read: got '%s'\n", buf); nvs_close(h); return;
    }
    printf("[PASS] nvs_write_read\n");

    nvs_erase_key(h, "test_key"); nvs_commit(h);
    len = sizeof(buf);
    if (nvs_get_str(h, "test_key", buf, &len) == ESP_ERR_NVS_NOT_FOUND)
        printf("[PASS] nvs_erase\n");
    else
        printf("[FAIL] nvs_erase: key still readable\n");

    nvs_close(h);
}

/* ------------------------------------------------------------------ */
/* TEST 2 — tag_write: dispatches on detected tag type                */
/*                                                                     */
/* MIFARE Classic 1K (ATQA=0004 SAK=08):                              */
/*   Auth sector 1 with default Key A → write block 4 → read back.   */
/*                                                                     */
/* NTAG213 (ATQA=4400 SAK=00):                                        */
/*   Write NDEF text record "IMB-TEST" to pages 4-8 → read back.     */
/* ------------------------------------------------------------------ */

static void tag_write_mifare(int cs, tag_t *tag)
{
    /* Try Gen1a magic backdoor first — Chinese clone cards ignore all
     * standard keys but respond to the 0x40/0x43 unlock sequence. */
    printf("[INFO] tag_write: trying Gen1a magic backdoor...\n");
    if (gen1a_unlock(cs)) {
        printf("[INFO] tag_write: Gen1a unlock OK — direct block write (no auth)\n");
        tag_write_gen1a(cs);
        return;
    }

    /* Gen1a failed — card may be in odd state; re-select before key probe */
    printf("[INFO] tag_write: Gen1a failed — re-selecting and probing standard keys\n");
    *tag = scan_tag(cs);
    if (!tag->found) {
        printf("[FAIL] tag_write: tag lost after Gen1a attempt\n");
        return;
    }

    static const uint8_t BLOCK = 4;
    static const uint8_t DATA[16] = "IMB-DRIVER-TEST!";

    uint8_t found_type, found_key[6];
    if (!mifare_probe_auth(cs, BLOCK, tag, &found_type, found_key)) {
        printf("[FAIL] tag_write: MIFARE auth failed with all %d key variants\n", N_PROBE_KEYS);
        return;
    }

    if (!mifare_write_block(cs, BLOCK, DATA)) {
        printf("[FAIL] tag_write: MIFARE write failed (block %d)\n", BLOCK);
        return;
    }

    /* Re-select + re-auth before read — auth state resets after write */
    *tag = scan_tag(cs);
    if (!tag->found || !mifare_auth(cs, BLOCK, tag->uid, found_type, found_key)) {
        printf("[FAIL] tag_write: MIFARE re-auth failed before readback\n");
        return;
    }

    uint8_t rb[16];
    if (!mifare_read_block(cs, BLOCK, rb)) {
        printf("[FAIL] tag_write: MIFARE readback failed\n");
        return;
    }

    if (memcmp(rb, DATA, 16) != 0) {
        printf("[FAIL] tag_write: readback mismatch\n");
        printf("[INFO] got:      "); for (int i=0;i<16;i++) printf("%02X ",rb[i]); printf("\n");
        printf("[INFO] expected: "); for (int i=0;i<16;i++) printf("%02X ",DATA[i]); printf("\n");
        return;
    }
    printf("[PASS] tag_write\n");
}

static const uint8_t NDEF_PAGES[5][4] = {
    {0x03, 0x0F, 0xD1, 0x01},
    {0x0B, 0x54, 0x02, 0x65},
    {0x6E, 0x49, 0x4D, 0x42},
    {0x2D, 0x54, 0x45, 0x53},
    {0x54, 0xFE, 0x00, 0x00},
};

static void tag_write_ntag213(int cs)
{
    for (int p = 0; p < 5; p++) {
        if (!ntag_write_page(cs, 4 + p, NDEF_PAGES[p])) {
            printf("[FAIL] tag_write: NTAG write failed at page %d\n", 4 + p);
            return;
        }
    }
    uint8_t rb[16], expected[16];
    if (!ntag_read_pages(cs, 4, rb)) { printf("[FAIL] tag_write: NTAG readback failed\n"); return; }
    for (int p = 0; p < 4; p++) memcpy(expected + p*4, NDEF_PAGES[p], 4);
    if (memcmp(rb, expected, 16) != 0) { printf("[FAIL] tag_write: NTAG readback mismatch\n"); return; }
    printf("[PASS] tag_write\n");
}

static void test_tag_write(int cs)
{
    printf("[INFO] tag_write: place tag on reader #1 NOW — scanning for 15 s...\n");

    tag_t tag = {0};
    for (int i = 0; i < 30 && !tag.found; i++) {
        tag = scan_tag(cs);
        if (!tag.found) {
            if (i % 4 == 3) printf("[INFO] tag_write: still scanning... (%d s elapsed)\n", (i+1)/2);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

    if (!tag.found) { printf("[SKIP] tag_write: no tag found within 15 s\n"); return; }

    printf("[INFO] tag_write: ATQA=%02X%02X SAK=%02X UID(%d):",
           tag.atqa[0], tag.atqa[1], tag.sak, tag.uid_len);
    for (int i = 0; i < tag.uid_len; i++) printf(" %02X", tag.uid[i]);
    printf("\n");

    /* MIFARE Classic 1K */
    if (tag.atqa[0] == 0x00 && tag.atqa[1] == 0x04 && tag.sak == 0x08) {
        printf("[INFO] tag_write: MIFARE Classic 1K — probing key + block write\n");
        tag_write_mifare(cs, &tag);
    }
    /* NTAG213 */
    else if (tag.atqa[0] == 0x44 && tag.atqa[1] == 0x00 && tag.sak == 0x00) {
        printf("[INFO] tag_write: NTAG213 — NDEF page write\n");
        tag_write_ntag213(cs);
    }
    else {
        printf("[SKIP] tag_write: unknown type (ATQA=%02X%02X SAK=%02X)\n",
               tag.atqa[0], tag.atqa[1], tag.sak);
    }
}

/* ------------------------------------------------------------------ */
/* TEST 3 — Deep sleep + timer wakeup                                 */
/* ------------------------------------------------------------------ */
static void deep_sleep_enter(void)
{
    s_boot_count = 1;
    printf("[INFO] deep_sleep: board going to sleep now (3 s timer)\n");
    printf("[INFO] deep_sleep: DO NOT press anything — board will wake automatically\n");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(150));   /* let UART drain */
    esp_sleep_enable_timer_wakeup(3ULL * 1000 * 1000);
    esp_deep_sleep_start();
}

static void deep_sleep_check(void)
{
    esp_sleep_wakeup_cause_t c = esp_sleep_get_wakeup_cause();
    printf("[INFO] deep_sleep: woke from sleep, checking cause...\n");
    if (c == ESP_SLEEP_WAKEUP_TIMER)
        printf("[PASS] deep_sleep\n");
    else
        printf("[FAIL] deep_sleep: cause=%d expected=%d (TIMER)\n",
               (int)c, (int)ESP_SLEEP_WAKEUP_TIMER);
    s_boot_count = 0;
}

/* ------------------------------------------------------------------ */
/* app_main                                                            */
/* ------------------------------------------------------------------ */
void app_main(void)
{
    /* Boot 2: woke from deep sleep */
    if (s_boot_count == 1) {
        deep_sleep_check();
        printf("[DONE]\n");
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    /* Boot 1: run all tests */
    printf("\n=== IMB Driver Integration Tests ===\n");
    printf("Tests: nvs_write_read / nvs_erase / tag_write / deep_sleep\n\n");

    gpio_config_t out = {
        .pin_bit_mask = (1ULL<<PIN_MOSI)|(1ULL<<PIN_SCK)|(1ULL<<PIN_CS1)|(1ULL<<PIN_CS2),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&out);
    gpio_config_t in = {
        .pin_bit_mask = (1ULL<<PIN_MISO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&in);
    gpio_set_level(PIN_CS1, 1);
    gpio_set_level(PIN_CS2, 1);
    vTaskDelay(pdMS_TO_TICKS(500));

    if (!pn532_probe(PIN_CS1))
        printf("[INFO] PN532 #1 probe failed — tag tests may skip\n");
    pn532_init(PIN_CS1);

    if (!pn532_probe(PIN_CS2))
        printf("[INFO] PN532 #2 probe failed — buzzer_events direction may not resolve\n");
    pn532_init(PIN_CS2);

    test_buzzer();
    test_nvs();
    test_tag_write(PIN_CS1);
    test_buzzer_events();

    /* deep_sleep_enter() does not return — second boot prints [DONE] */
    deep_sleep_enter();
}
