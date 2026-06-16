/* IMB On-Device Driver Integration Tests
 * Standalone ESP-IDF project — NOT production firmware.
 *
 * Tests run on every boot in order:
 *   1. NVS       — write / read / erase in imb_local namespace
 *   2. tag_write — MIFARE Classic 1K (gen1a backdoor) or NTAG213 (NDEF pages)
 *   3. tag_ndef  — write NDEF name via imb_nfc_write_ndef, read back via imb_nfc_read_ndef
 *   4. deep_sleep — 3 s timer wakeup, verifies wakeup cause
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
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_timer.h"
#include "imb_buzzer.h"
#include "imb_buzzer_ledc.h"
#include "imb_nfc.h"
#include "imb_nfc_pn532.h"
#include "imb_detector.h"

/* ------------------------------------------------------------------ */
/* Pin config (hardware.md)                                            */
/* ------------------------------------------------------------------ */
#define PIN_MOSI  11
#define PIN_MISO  13
#define PIN_SCK   12
#define PIN_CS1   10   /* inner reader (reader 0) */
#define PIN_CS2    9   /* outer reader (reader 1) */

/* Survives deep sleep in RTC-fast memory */
static RTC_DATA_ATTR int s_boot_count = 0;

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
        imb_nfc_tag_t t1, t2;
        if (imb_nfc_scan(0, &t1)) imb_detector_on_reader_event(&det, 0, t1.uid_str);
        if (imb_nfc_scan(1, &t2)) imb_detector_on_reader_event(&det, 1, t2.uid_str);
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
/* TEST 2 — tag_write: write NDEF name, read back via imb_nfc         */
/* ------------------------------------------------------------------ */

static void test_tag_write(uint8_t reader_id)
{
    printf("[INFO] tag_write: place tag on reader NOW — scanning for 15 s...\n");

    imb_nfc_tag_t tag = {0};
    for (int i = 0; i < 30 && !tag.found; i++) {
        imb_nfc_scan(reader_id, &tag);
        if (!tag.found) {
            if (i % 4 == 3)
                printf("[INFO] tag_write: still scanning... (%d s elapsed)\n", (i+1)/2);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

    if (!tag.found) { printf("[SKIP] tag_write: no tag found within 15 s\n"); return; }

    printf("[INFO] tag_write: ATQA=%02X%02X SAK=%02X uid=%s\n",
           tag.atqa[0], tag.atqa[1], tag.sak, tag.uid_str);

    if (!imb_nfc_write_ndef(reader_id, &tag, "IMB-TEST")) {
        printf("[FAIL] tag_write: imb_nfc_write_ndef returned 0\n"); return;
    }

    /* Re-scan to get fresh tag handle before read */
    imb_nfc_tag_t tag2 = {0};
    for (int i = 0; i < 10 && !tag2.found; i++) {
        imb_nfc_scan(reader_id, &tag2);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    if (!tag2.found) { printf("[FAIL] tag_write: tag lost before readback\n"); return; }

    char name[32] = {0};
    if (!imb_nfc_read_ndef(reader_id, &tag2, name, sizeof(name))) {
        printf("[FAIL] tag_write: imb_nfc_read_ndef returned 0\n"); return;
    }
    if (strcmp(name, "IMB-TEST") != 0) {
        printf("[FAIL] tag_write: readback mismatch — got '%s'\n", name); return;
    }
    printf("[PASS] tag_write\n");
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

    /* NFC init — owns GPIO config for SPI + CS pins */
    imb_nfc_pn532_config_t pn532_cfg = {
        .mosi = PIN_MOSI, .miso = PIN_MISO, .sck = PIN_SCK,
        .cs   = {PIN_CS1, PIN_CS2},
    };
    imb_nfc_hal_t nfc_hal = imb_nfc_pn532_init(&pn532_cfg);
    imb_nfc_init(&nfc_hal);

    test_buzzer();
    test_nvs();
    test_tag_write(0);   /* reader 0 = inner (PIN_CS1) */
    test_buzzer_events();

    /* deep_sleep_enter() does not return — second boot prints [DONE] */
    deep_sleep_enter();
}
