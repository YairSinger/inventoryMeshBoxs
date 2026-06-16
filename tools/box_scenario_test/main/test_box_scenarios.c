/*
 * box_scenario_test — end-to-end integration test for the IMB box firmware core.
 *
 * Wiring: MOSI=GPIO11  MISO=GPIO13  SCK=GPIO12
 *         Inner reader (reader 0) CS=GPIO10
 *         Outer reader (reader 1) CS=GPIO9
 *
 * Two-boot flow controlled by NVS key "test_phase":
 *   Boot 1 (phase == 0):
 *     Phase A — Prewrite 9 cards ("card1".."card9") on reader 0
 *     Phase B — Register each card in NVS registry
 *     Prompt RESET → write "test_phase" = 1 to NVS
 *   Boot 2 (phase == 1):
 *     Phase C — Verify all 9 items survive reboot
 *     Phase D — Scenario tests (INSERT / EXTRACT / AMBIGUOUS / unknown / two-card)
 *     Phase E — Deep sleep 5 s → wake → verify registry still intact
 *   Boot 3 (phase == 2, woken by timer):
 *     Verify registry, print final results, reset phase to 0.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "imb_nfc.h"
#include "imb_nfc_pn532.h"
#include "imb_detector.h"
#include "imb_registry.h"
#include "imb_session.h"
#include "imb_types.h"

/* ── Hardware config ─────────────────────────────────────────────────── */
#define PIN_MOSI  11
#define PIN_MISO  13
#define PIN_SCK   12
#define PIN_CS0   10   /* inner reader */
#define PIN_CS1    9   /* outer reader */

#define R0_LABEL  "READER 0 — inner — CS GPIO 10"
#define R1_LABEL  "READER 1 — outer — CS GPIO 9"

/* ── NVS namespaces / keys ────────────────────────────────────────────── */
#define NVS_NS_LOCAL   "imb_local"
#define NVS_NS_TEST    "imb_test"
#define NVS_KEY_PHASE  "test_phase"

/* ── Timing ──────────────────────────────────────────────────────────── */
#define TAG_WAIT_MS       20000
#define MOVE_WINDOW_MS     3000
#define SCENARIO_WAIT_MS  12000
#define SLEEP_DURATION_S       5

/* ── Pass/Fail ───────────────────────────────────────────────────────── */
static int g_pass = 0;
static int g_fail = 0;

#define CHECK(label, cond) \
    do { if (cond) { printf("  PASS  %s\n", label); g_pass++; } \
         else      { printf("  FAIL  %s\n", label); g_fail++; } } while(0)

/* ── Action prompt ───────────────────────────────────────────────────── */
static void prompt(const char *msg)
{
    printf("\n=== ACTION REQUIRED ===\n>>> %s\n=======================\n", msg);
}

/* ── Millisecond clock ───────────────────────────────────────────────── */
static int64_t get_ms(void) { return esp_timer_get_time() / 1000; }
static uint32_t det_get_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* ── Detector callback shim ──────────────────────────────────────────── */
static imb_scan_event_t g_last_event;
static volatile int     g_event_ready;

static void on_detector_event(const imb_scan_event_t *evt, void *ctx)
{
    (void)ctx;
    g_last_event  = *evt;
    g_event_ready = 1;
}

static const char *dir_name(imb_direction_e d)
{
    switch (d) {
        case IMB_INSERT:    return "INSERT";
        case IMB_EXTRACT:   return "EXTRACT";
        case IMB_AMBIGUOUS: return "AMBIGUOUS";
        default:            return "UNKNOWN";
    }
}

/* ── NVS HAL for imb_registry ─────────────────────────────────────────── */
static imb_reg_err_e reg_load(const char *key, imb_item_t *out, void *ctx)
{
    (void)ctx;
    nvs_handle_t h;
    if (nvs_open(NVS_NS_LOCAL, NVS_READONLY, &h) != ESP_OK) return IMB_REG_ERR_HAL;
    size_t len = sizeof(imb_item_t);
    esp_err_t e = nvs_get_blob(h, key, out, &len);
    nvs_close(h);
    if (e == ESP_ERR_NVS_NOT_FOUND) return IMB_REG_ERR_NOT_FOUND;
    return e == ESP_OK ? IMB_REG_OK : IMB_REG_ERR_HAL;
}
static imb_reg_err_e reg_save(const char *key, const imb_item_t *in, void *ctx)
{
    (void)ctx;
    nvs_handle_t h;
    if (nvs_open(NVS_NS_LOCAL, NVS_READWRITE, &h) != ESP_OK) return IMB_REG_ERR_HAL;
    esp_err_t e = nvs_set_blob(h, key, in, sizeof(imb_item_t));
    if (e == ESP_OK) nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK ? IMB_REG_OK : IMB_REG_ERR_HAL;
}
static imb_reg_err_e reg_erase(const char *key, void *ctx)
{
    (void)ctx;
    nvs_handle_t h;
    if (nvs_open(NVS_NS_LOCAL, NVS_READWRITE, &h) != ESP_OK) return IMB_REG_ERR_HAL;
    esp_err_t e = nvs_erase_key(h, key);
    if (e == ESP_OK) nvs_commit(h);
    nvs_close(h);
    if (e == ESP_ERR_NVS_NOT_FOUND) return IMB_REG_ERR_NOT_FOUND;
    return e == ESP_OK ? IMB_REG_OK : IMB_REG_ERR_HAL;
}
static imb_reg_err_e reg_load_u16(const char *key, uint16_t *out, void *ctx)
{
    (void)ctx;
    nvs_handle_t h;
    if (nvs_open(NVS_NS_LOCAL, NVS_READONLY, &h) != ESP_OK) return IMB_REG_ERR_HAL;
    esp_err_t e = nvs_get_u16(h, key, out);
    nvs_close(h);
    if (e == ESP_ERR_NVS_NOT_FOUND) return IMB_REG_ERR_NOT_FOUND;
    return e == ESP_OK ? IMB_REG_OK : IMB_REG_ERR_HAL;
}
static imb_reg_err_e reg_save_u16(const char *key, uint16_t val, void *ctx)
{
    (void)ctx;
    nvs_handle_t h;
    if (nvs_open(NVS_NS_LOCAL, NVS_READWRITE, &h) != ESP_OK) return IMB_REG_ERR_HAL;
    esp_err_t e = nvs_set_u16(h, key, val);
    if (e == ESP_OK) nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK ? IMB_REG_OK : IMB_REG_ERR_HAL;
}
static imb_nvs_hal_t g_nvs_hal = {
    .load     = reg_load,
    .save     = reg_save,
    .erase    = reg_erase,
    .load_u16 = reg_load_u16,
    .save_u16 = reg_save_u16,
    .ctx      = NULL,
};

/* ── Helpers ─────────────────────────────────────────────────────────── */

static int wait_for_tag(uint8_t reader_id, imb_nfc_tag_t *out, int timeout_ms)
{
    int64_t deadline = get_ms() + timeout_ms;
    while (get_ms() < deadline) {
        if (imb_nfc_scan(reader_id, out)) return 1;
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    return 0;
}

static int wait_for_no_tag(uint8_t reader_id, int timeout_ms)
{
    int64_t deadline = get_ms() + timeout_ms;
    imb_nfc_tag_t t;
    while (get_ms() < deadline) {
        if (!imb_nfc_scan(reader_id, &t)) return 1;
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    return 0;
}

/* Poll both readers, feed detector, wait for an event or timeout. */
static int run_until_event(imb_detector_t *det, imb_scan_event_t *evt_out,
                           int timeout_ms)
{
    g_event_ready = 0;
    int64_t deadline = get_ms() + timeout_ms;
    imb_nfc_tag_t t0, t1;
    while (get_ms() < deadline) {
        if (imb_nfc_scan(0, &t0)) imb_detector_on_reader_event(det, 0, t0.uid_str);
        if (imb_nfc_scan(1, &t1)) imb_detector_on_reader_event(det, 1, t1.uid_str);
        imb_detector_tick(det);
        if (g_event_ready) {
            *evt_out = g_last_event;
            return 1;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return 0;
}

/* ── NVS phase helpers ────────────────────────────────────────────────── */
static uint8_t read_test_phase(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_TEST, NVS_READONLY, &h) != ESP_OK) return 0;
    uint8_t val = 0;
    nvs_get_u8(h, NVS_KEY_PHASE, &val);
    nvs_close(h);
    return val;
}
static void write_test_phase(uint8_t phase)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_TEST, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, NVS_KEY_PHASE, phase);
    nvs_commit(h);
    nvs_close(h);
}

/* ══════════════════════════════════════════════════════════════════════
 * PHASE A: prewrite "card1".."card9" on reader 0
 * ══════════════════════════════════════════════════════════════════════ */
#define N_CARDS 9
static char g_uids[N_CARDS][IMB_UID_LEN];
static char g_names[N_CARDS][IMB_NAME_LEN];

static int phase_a_prewrite(void)
{
    printf("\n══════════════════════════════════════\n");
    printf("  PHASE A — Prewrite %d cards on %s\n", N_CARDS, R0_LABEL);
    printf("══════════════════════════════════════\n");

    int written = 0;
    for (int i = 0; i < N_CARDS; i++) {
        snprintf(g_names[i], sizeof(g_names[i]), "card%d", i + 1);

        char msg[100];
        snprintf(msg, sizeof(msg),
                 "Place card %d/%d on %s  (will write '%s')",
                 i + 1, N_CARDS, R0_LABEL, g_names[i]);
        prompt(msg);

        imb_nfc_tag_t t;
        if (!wait_for_tag(0, &t, TAG_WAIT_MS)) {
            printf("  ERROR: no tag — skipping card %d\n", i + 1);
            continue;
        }
        printf("  Detected: UID=%s  SAK=0x%02X\n", t.uid_str, t.sak);

        if (!imb_nfc_write_ndef(0, &t, g_names[i])) {
            printf("  ERROR: write_ndef failed for card %d\n", i + 1);
            continue;
        }

        char readback[IMB_NAME_LEN] = {0};
        imb_nfc_tag_t t2;
        if (!wait_for_tag(0, &t2, 3000) ||
            !imb_nfc_read_ndef(0, &t2, readback, sizeof(readback)) ||
            strcmp(readback, g_names[i]) != 0) {
            printf("  ERROR: readback mismatch for card %d (got '%s')\n",
                   i + 1, readback);
            continue;
        }

        strncpy(g_uids[i], t.uid_str, IMB_UID_LEN - 1);
        printf("  OK: '%s' written — UID=%s\n", g_names[i], g_uids[i]);
        written++;

        prompt("Remove card from reader (keep it ready for Phase B).");
        wait_for_no_tag(0, 5000);
    }

    printf("\nPhase A: %d/%d cards prewritten.\n", written, N_CARDS);
    return written;
}

/* ══════════════════════════════════════════════════════════════════════
 * PHASE B: register each card — scan on reader 0, read NDEF → registry
 * ══════════════════════════════════════════════════════════════════════ */
static int phase_b_register(imb_registry_t *reg)
{
    printf("\n══════════════════════════════════════\n");
    printf("  PHASE B — Register %d cards in NVS\n", N_CARDS);
    printf("══════════════════════════════════════\n");

    int registered = 0;
    for (int i = 0; i < N_CARDS; i++) {
        char msg[100];
        snprintf(msg, sizeof(msg),
                 "Place '%s' on %s to register it (%d/%d)",
                 g_names[i], R0_LABEL, i + 1, N_CARDS);
        prompt(msg);

        imb_nfc_tag_t t;
        if (!wait_for_tag(0, &t, TAG_WAIT_MS)) {
            printf("  ERROR: no tag for card %d\n", i + 1);
            continue;
        }

        char name[IMB_NAME_LEN] = {0};
        if (!imb_nfc_read_ndef(0, &t, name, sizeof(name))) {
            printf("  ERROR: read_ndef failed for UID=%s\n", t.uid_str);
            continue;
        }

        imb_item_t item;
        strncpy(item.uid,  t.uid_str, IMB_UID_LEN - 1);  item.uid[IMB_UID_LEN-1] = '\0';
        strncpy(item.name, name,      IMB_NAME_LEN - 1);  item.name[IMB_NAME_LEN-1] = '\0';

        if (imb_registry_add(reg, &item) != IMB_REG_OK) {
            printf("  ERROR: registry_add failed (UID=%s)\n", t.uid_str);
            continue;
        }

        printf("  Registered: uid=%-14s  name='%s'\n", item.uid, item.name);
        registered++;

        prompt("Remove card from reader.");
        wait_for_no_tag(0, 5000);
    }

    printf("\nPhase B: %d/%d cards registered. Registry count=%u\n",
           registered, N_CARDS, imb_registry_count(reg));
    return registered;
}

/* ══════════════════════════════════════════════════════════════════════
 * PHASE C: verify registry survives reboot
 * ══════════════════════════════════════════════════════════════════════ */
static void phase_c_verify_registry(imb_registry_t *reg)
{
    printf("\n══════════════════════════════════════\n");
    printf("  PHASE C — Registry reboot persistence\n");
    printf("══════════════════════════════════════\n");

    uint16_t count = imb_registry_count(reg);
    printf("  Registry loaded: %u items\n", count);
    CHECK("C registry survives reboot (count == 9)", count == N_CARDS);

    imb_item_t items[IMB_REGISTRY_MAX_ITEMS];
    uint16_t n = 0;
    imb_registry_get_all(reg, items, &n);
    printf("  Items in registry:\n");
    for (uint16_t i = 0; i < n; i++)
        printf("    [%2u] uid=%-14s  name='%s'\n", i, items[i].uid, items[i].name);
}

/* ══════════════════════════════════════════════════════════════════════
 * PHASE D: scenario tests
 * ══════════════════════════════════════════════════════════════════════ */
static void phase_d_scenarios(imb_registry_t *reg)
{
    printf("\n══════════════════════════════════════\n");
    printf("  PHASE D — Scenario tests\n");
    printf("══════════════════════════════════════\n");

    imb_detector_t det;
    imb_detector_init(&det, MOVE_WINDOW_MS, det_get_ms, on_detector_event, NULL);
    imb_scan_event_t evt;

    /* ── D1: INSERT ─────────────────────────────────────────────────── */
    prompt("INSERT: swipe any registered card from OUTSIDE to INSIDE.\n"
           ">>> Touch " R1_LABEL " (outer) FIRST, then " R0_LABEL " (inner) within 3 s.");
    {
        int got = run_until_event(&det, &evt, SCENARIO_WAIT_MS);
        CHECK("D1 INSERT event fires", got && evt.dir == IMB_INSERT);
        if (got) {
            printf("  event=%-10s  uid=%s\n", dir_name(evt.dir), evt.uid);
            imb_item_t item;
            int known = imb_registry_get(reg, evt.uid, &item) == IMB_REG_OK;
            CHECK("D1 INSERT uid is in registry", known);
            if (known) printf("  Name: '%s'\n", item.name);
        }
    }
    imb_detector_init(&det, MOVE_WINDOW_MS, det_get_ms, on_detector_event, NULL);

    /* ── D2: EXTRACT ────────────────────────────────────────────────── */
    prompt("EXTRACT: swipe any registered card from INSIDE to OUTSIDE.\n"
           ">>> Touch " R0_LABEL " (inner) FIRST, then " R1_LABEL " (outer) within 3 s.");
    {
        int got = run_until_event(&det, &evt, SCENARIO_WAIT_MS);
        CHECK("D2 EXTRACT event fires", got && evt.dir == IMB_EXTRACT);
        if (got) printf("  event=%-10s  uid=%s\n", dir_name(evt.dir), evt.uid);
    }
    imb_detector_init(&det, MOVE_WINDOW_MS, det_get_ms, on_detector_event, NULL);

    /* ── D3: AMBIGUOUS ──────────────────────────────────────────────── */
    prompt("AMBIGUOUS: place any card on ONE reader only. Do NOT cross to the other.\n"
           ">>> Hold still until the 3 s window expires (~5 s total).");
    {
        int got = run_until_event(&det, &evt, MOVE_WINDOW_MS + 5000);
        CHECK("D3 AMBIGUOUS event fires", got && evt.dir == IMB_AMBIGUOUS);
        if (got) {
            printf("  event=%-10s  uid=%s\n", dir_name(evt.dir), evt.uid);

            imb_session_t sess;
            imb_session_init(&sess);
            imb_session_apply(&sess, &evt);
            imb_entry_u amb[IMB_REGISTRY_MAX_ITEMS];
            uint16_t cnt = imb_session_get_ambiguous(&sess, amb, IMB_REGISTRY_MAX_ITEMS);
            CHECK("D3 session tracks ambiguous uid", cnt > 0);
        }
    }
    imb_detector_init(&det, MOVE_WINDOW_MS, det_get_ms, on_detector_event, NULL);

    /* ── D4: AMBIGUOUS in session resolved by a subsequent directed event ── */
    prompt("AMBIGUOUS→RESOLVED: place a card on ONE reader only (will go AMBIGUOUS),\n"
           ">>> then swipe it fully through (outer→inner or inner→outer) to resolve.");
    {
        imb_session_t sess;
        imb_session_init(&sess);

        /* Step 1: wait for the AMBIGUOUS timeout */
        int got_amb = run_until_event(&det, &evt, MOVE_WINDOW_MS + 5000);
        CHECK("D4 first event is AMBIGUOUS", got_amb && evt.dir == IMB_AMBIGUOUS);

        int resolved = 0;
        imb_scan_event_t evt2 = {0};
        if (got_amb && evt.dir == IMB_AMBIGUOUS) {
            imb_session_apply(&sess, &evt);
            imb_entry_u amb[IMB_REGISTRY_MAX_ITEMS];
            uint16_t amb_cnt = imb_session_get_ambiguous(&sess, amb,
                                                         IMB_REGISTRY_MAX_ITEMS);
            CHECK("D4 session ambiguous set has uid after AMBIGUOUS event", amb_cnt > 0);
            printf("  Ambiguous uid=%s — now swipe fully through...\n", evt.uid);

            /* Step 2: wait for the resolving INSERT or EXTRACT */
            imb_detector_init(&det, MOVE_WINDOW_MS, det_get_ms, on_detector_event, NULL);
            resolved = run_until_event(&det, &evt2, SCENARIO_WAIT_MS);
            if (resolved && (evt2.dir == IMB_INSERT || evt2.dir == IMB_EXTRACT)) {
                imb_session_apply(&sess, &evt2);
                uint16_t amb_after = imb_session_get_ambiguous(&sess, amb,
                                                               IMB_REGISTRY_MAX_ITEMS);
                uint16_t pres_cnt = imb_session_get_present(&sess, amb,
                                                            IMB_REGISTRY_MAX_ITEMS);
                CHECK("D4 ambiguous cleared after directed event", amb_after == 0);
                if (evt2.dir == IMB_INSERT)
                    CHECK("D4 INSERT: uid now in present set", pres_cnt > 0);
                else
                    CHECK("D4 EXTRACT: uid not in present set", pres_cnt == 0);
            }
        }
        CHECK("D4 ambiguous resolves to directed event",
              resolved && (evt2.dir == IMB_INSERT || evt2.dir == IMB_EXTRACT));
        if (resolved)
            printf("  Resolved to: %s  uid=%s\n", dir_name(evt2.dir), evt2.uid);
    }
    imb_detector_init(&det, MOVE_WINDOW_MS, det_get_ms, on_detector_event, NULL);

    /* ── D5: Unknown card ───────────────────────────────────────────── */
    prompt("UNKNOWN CARD: place a card that was NOT prewritten (not card1-card9)\n"
           ">>> on either reader. It should fire AMBIGUOUS and NOT be in the registry.");
    {
        int got = run_until_event(&det, &evt, SCENARIO_WAIT_MS);
        if (got) {
            imb_item_t item;
            int known = imb_registry_get(reg, evt.uid, &item) == IMB_REG_OK;
            printf("  event=%-10s  uid=%s  in_registry=%s\n",
                   dir_name(evt.dir), evt.uid, known ? "YES (known)" : "NO (anonymous)");
            CHECK("D5 event fires for unknown card", 1);
            CHECK("D5 unknown card uid NOT in registry", !known);
        } else {
            printf("  No event — skipping D5\n");
        }
    }
    imb_detector_init(&det, MOVE_WINDOW_MS, det_get_ms, on_detector_event, NULL);

    /* ── D6: Two cards simultaneously ───────────────────────────────── */
    prompt("TWO CARDS: place one registered card on each reader simultaneously.\n"
           ">>> " R0_LABEL " + " R1_LABEL " at the same time.");
    {
        vTaskDelay(pdMS_TO_TICKS(1500));
        imb_nfc_tag_t t0, t1;
        int f0 = imb_nfc_scan(0, &t0);
        int f1 = imb_nfc_scan(1, &t1);
        printf("  Reader 0: %-14s  Reader 1: %s\n",
               f0 ? t0.uid_str : "(none)", f1 ? t1.uid_str : "(none)");
        CHECK("D6 inner reader detects card", f0);
        CHECK("D6 outer reader detects card", f1);
        if (f0 && f1)
            CHECK("D6 two distinct cards",
                  strcmp(t0.uid_str, t1.uid_str) != 0);
    }

    printf("\nPhase D done. Pass=%d  Fail=%d\n", g_pass, g_fail);
}

/* ══════════════════════════════════════════════════════════════════════
 * PHASE E: deep sleep → wake → verify registry intact
 * ══════════════════════════════════════════════════════════════════════ */
static void phase_e_sleep(void)
{
    printf("\n══════════════════════════════════════\n");
    printf("  PHASE E — Deep sleep %d s then wake\n", SLEEP_DURATION_S);
    printf("══════════════════════════════════════\n");

    write_test_phase(2);
    printf("  Entering deep sleep...\n");
    esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_DURATION_S * 1000000ULL);
    esp_deep_sleep_start();
}

static void phase_e_verify_wake(imb_registry_t *reg)
{
    printf("\n══════════════════════════════════════\n");
    printf("  PHASE E — Woke from deep sleep\n");
    printf("══════════════════════════════════════\n");

    uint16_t count = imb_registry_count(reg);
    printf("  Registry count after sleep/wake: %u\n", count);
    CHECK("E registry survives deep sleep", count == N_CARDS);

    printf("\n══════════════════════════════════════════\n");
    printf("  ALL PHASES DONE.  PASS=%d  FAIL=%d\n", g_pass, g_fail);
    printf("══════════════════════════════════════════\n");

    write_test_phase(0);  /* reset for next full run */
    printf("  Phase reset to 0. Power-cycle to start fresh.\n");
}

/* ══════════════════════════════════════════════════════════════════════
 * app_main
 * ══════════════════════════════════════════════════════════════════════ */
void app_main(void)
{
    printf("\n========================================\n");
    printf("  IMB Box Scenario Test\n");
    printf("  MOSI=GPIO%d  MISO=GPIO%d  SCK=GPIO%d\n",
           PIN_MOSI, PIN_MISO, PIN_SCK);
    printf("  Inner reader: CS GPIO%d\n", PIN_CS0);
    printf("  Outer reader: CS GPIO%d\n", PIN_CS1);
    printf("========================================\n");

    /* NVS */
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* NFC */
    imb_nfc_pn532_config_t pn532_cfg = {
        .mosi = PIN_MOSI, .miso = PIN_MISO, .sck = PIN_SCK,
        .cs   = {PIN_CS0, PIN_CS1},
    };
    imb_nfc_hal_t hal = imb_nfc_pn532_init(&pn532_cfg);
    imb_nfc_init(&hal);
    printf("PN532 init — both readers awake.\n");

    /* Registry */
    imb_registry_t reg;
    imb_registry_init(&reg, &g_nvs_hal, IMB_REGISTRY_MAX_ITEMS);

    uint8_t phase = read_test_phase();
    printf("Boot phase: %u\n\n", phase);

    if (phase == 2) {
        /* Woke from deep sleep */
        phase_e_verify_wake(&reg);
        return;
    }

    if (phase == 1) {
        /* Post-reboot: verify + scenarios + sleep */
        phase_c_verify_registry(&reg);
        phase_d_scenarios(&reg);
        phase_e_sleep();
        return;   /* never reached — deep sleep */
    }

    /* phase == 0: first boot */
    /* Clear any stale registry */
    {
        nvs_handle_t h;
        if (nvs_open(NVS_NS_LOCAL, NVS_READWRITE, &h) == ESP_OK) {
            nvs_erase_all(h);
            nvs_commit(h);
            nvs_close(h);
        }
    }
    imb_registry_init(&reg, &g_nvs_hal, IMB_REGISTRY_MAX_ITEMS);

    int registered = phase_b_register(&reg);

    write_test_phase(1);

    printf("\n══════════════════════════════════════════\n");
    printf("  Phase B done: %d registered.\n", registered);
    printf("  >>> RESET or POWER-CYCLE the board now.\n");
    printf("  >>> Next boot will run Phase C (reboot check), D (scenarios), E (sleep).\n");
    printf("══════════════════════════════════════════\n");

    for (;;) {
        printf("  Waiting for reset...\n");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
