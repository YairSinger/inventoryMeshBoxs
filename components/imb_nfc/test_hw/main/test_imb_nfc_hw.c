#include "unity.h"
#include "imb_nfc.h"
#include "imb_nfc_pn532.h"
#include "imb_detector.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>

/* ── GPIO pin map (docs/hardware.md) ────────────────────────────────── */
#define NFC_MOSI  11
#define NFC_MISO  13
#define NFC_SCK   12
#define NFC_CS0   10   /* inner reader */
#define NFC_CS1    9   /* outer reader */

/* Human-readable reader labels used in every prompt */
#define STRINGIFY_(x) #x
#define STRINGIFY(x)  STRINGIFY_(x)
#define R0_LABEL  "READER 0 — inner — CS GPIO " STRINGIFY(NFC_CS0)
#define R1_LABEL  "READER 1 — outer — CS GPIO " STRINGIFY(NFC_CS1)

/* Window long enough for a human to move a card between the two readers */
#define DETECTOR_WINDOW_MS   3000

/* Scan poll interval and test timeouts */
#define POLL_MS               50
#define TAG_WAIT_MS        15000
#define MOVEMENT_WAIT_MS   10000   /* time given to complete a card move */
#define AMBIGUOUS_WAIT_MS   5000   /* how long to hold still for AMBIGUOUS */

/* ── Globals ─────────────────────────────────────────────────────────── */

/* UIDs discovered during scan-group tests; reused by find_by_uid tests */
static char g_uid_r0[IMB_NFC_UID_STR_LEN];
static char g_uid_r1[IMB_NFC_UID_STR_LEN];

/* Debounce state — track last-seen UID per reader so we only fire on NEW appearances */
static char g_seen_r0[IMB_NFC_UID_STR_LEN];
static char g_seen_r1[IMB_NFC_UID_STR_LEN];

/* Detector event capture */
static imb_scan_event_t g_last_event;
static int              g_event_count;

/* ── Helpers ─────────────────────────────────────────────────────────── */

static void prompt(const char *msg)
{
    printf("\n\n=== ACTION REQUIRED ===\n>>> %s\n=======================\n\n", msg);
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(3000));
}

static uint32_t get_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* Poll reader_id until any tag appears or timeout. Returns 1 if found. */
static int wait_for_tag(uint8_t reader_id, imb_nfc_tag_t *out, int timeout_ms)
{
    for (int elapsed = 0; elapsed < timeout_ms; elapsed += POLL_MS) {
        if (imb_nfc_scan(reader_id, out)) return 1;
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
    return 0;
}

/* Poll until no tag is present on reader_id or timeout. */
static int wait_for_no_tag(uint8_t reader_id, int timeout_ms)
{
    imb_nfc_tag_t t;
    for (int elapsed = 0; elapsed < timeout_ms; elapsed += POLL_MS) {
        if (!imb_nfc_scan(reader_id, &t)) return 1;
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
    return 0;
}

static void on_det_event(const imb_scan_event_t *e, void *ctx)
{
    (void)ctx;
    g_last_event = *e;
    g_event_count++;
}

static void reset_detector_state(void)
{
    g_event_count = 0;
    memset(&g_last_event, 0, sizeof(g_last_event));
    g_seen_r0[0] = '\0';
    g_seen_r1[0] = '\0';
}

/*
 * Scan both readers; feed only NEW appearances (rising edge) into the detector.
 * Clearing g_seen_* when a tag is removed ensures the next placement counts as new.
 */
static void scan_and_feed(imb_detector_t *det)
{
    imb_nfc_tag_t t;

    if (imb_nfc_scan(0, &t)) {
        if (strcmp(g_seen_r0, t.uid_str) != 0) {
            strncpy(g_seen_r0, t.uid_str, IMB_NFC_UID_STR_LEN);
            imb_detector_on_reader_event(det, 0, t.uid_str);
        }
    } else {
        g_seen_r0[0] = '\0';
    }

    if (imb_nfc_scan(1, &t)) {
        if (strcmp(g_seen_r1, t.uid_str) != 0) {
            strncpy(g_seen_r1, t.uid_str, IMB_NFC_UID_STR_LEN);
            imb_detector_on_reader_event(det, 1, t.uid_str);
        }
    } else {
        g_seen_r1[0] = '\0';
    }

    imb_detector_tick(det);
}

/* Run the scan loop until an event fires or timeout. Returns 1 if event fired. */
static int run_until_event(imb_detector_t *det, int timeout_ms)
{
    for (int elapsed = 0; elapsed < timeout_ms; elapsed += POLL_MS) {
        scan_and_feed(det);
        if (g_event_count > 0) return 1;
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
    return 0;
}

void setUp(void)  {}
void tearDown(void) {}

/* ── Prewrite helper (runs before Unity) ─────────────────────────────
 * Places a known NDEF name onto a card using our writer.
 * - Scans the card and prints UID + SAK.
 * - Attempts to read any existing NDEF first; the debug printf in
 *   imb_nfc_read_ndef will dump the raw page-4 bytes regardless.
 * - Writes our format, reads back, verifies.
 * Returns 1 on success, 0 on any failure.
 * ─────────────────────────────────────────────────────────────────── */
static int prewrite_card(uint8_t reader_id, const char *name, const char *label)
{
    printf("\n--- prewrite '%s' on %s ---\n", name, label);
    fflush(stdout);

    imb_nfc_tag_t t;
    if (!wait_for_tag(reader_id, &t, TAG_WAIT_MS)) {
        printf("  ERROR: no tag detected\n");
        return 0;
    }
    printf("  UID=%s  SAK=0x%02X\n", t.uid_str, t.sak);

    if (!imb_nfc_write_ndef(reader_id, &t, name)) {
        printf("  ERROR: write_ndef failed\n");
        return 0;
    }

    char readback[IMB_NFC_NAME_MAX];
    if (!imb_nfc_read_ndef(reader_id, &t, readback, sizeof(readback))) {
        printf("  ERROR: read_ndef after write failed\n");
        return 0;
    }
    if (strcmp(readback, name) != 0) {
        printf("  ERROR: readback '%s' != '%s'\n", readback, name);
        return 0;
    }

    printf("  OK: '%s' written and verified  UID=%s  SAK=0x%02X\n\n", name, t.uid_str, t.sak);
    return 1;
}

/* ═══════════════════════════════════════════════════════════════════════
 * GROUP 0 — Verify prewritten cards are readable
 *
 * Cards were stamped with our NDEF format in the prewrite phase above.
 * These tests confirm the round-trip: write (done) → scan → read.
 * ═════════════════════════════════════════════════════════════════════*/

void test_read_card1_on_inner_reader(void)
{
    imb_nfc_tag_t t;
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, wait_for_tag(0, &t, TAG_WAIT_MS),
        "No tag on " R0_LABEL);
    char name[IMB_NFC_NAME_MAX];
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, imb_nfc_read_ndef(0, &t, name, sizeof(name)),
        "read_ndef failed on prewritten card");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("card1", name,
        "Expected 'card1' on inner reader");
    printf("    UID=%s  name='%s'\n", t.uid_str, name);
    fflush(stdout);
}

void test_read_card2_on_outer_reader(void)
{
    imb_nfc_tag_t t;
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, wait_for_tag(1, &t, TAG_WAIT_MS),
        "No tag on " R1_LABEL);
    char name[IMB_NFC_NAME_MAX];
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, imb_nfc_read_ndef(1, &t, name, sizeof(name)),
        "read_ndef failed on prewritten card");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("card2", name,
        "Expected 'card2' on outer reader");
    printf("    UID=%s  name='%s'\n", t.uid_str, name);
    fflush(stdout);
}

/* ═══════════════════════════════════════════════════════════════════════
 * GROUP 1 — No tags present
 * ═════════════════════════════════════════════════════════════════════*/

void test_scan_no_tag_inner_reader_returns_0(void)
{
    imb_nfc_tag_t t;
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, imb_nfc_scan(0, &t),
        R0_LABEL " reported a tag but nothing was placed");
}

void test_scan_no_tag_uid_str_empty(void)
{
    imb_nfc_tag_t t;
    imb_nfc_scan(0, &t);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", t.uid_str,
        "uid_str must be empty when no tag found");
}

void test_scan_no_tag_outer_reader_returns_0(void)
{
    imb_nfc_tag_t t;
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, imb_nfc_scan(1, &t),
        R1_LABEL " reported a tag but nothing was placed");
}

void test_find_by_uid_no_tag_returns_0(void)
{
    imb_nfc_tag_t t;
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, imb_nfc_find_by_uid(0, "AABBCCDD", &t),
        "find_by_uid must return 0 when no tag is present");
}

/* ═══════════════════════════════════════════════════════════════════════
 * GROUP 2 — Tag on inner reader only
 * ═════════════════════════════════════════════════════════════════════*/

void test_scan_tag_found_inner_reader(void)
{
    imb_nfc_tag_t t;
    int found = wait_for_tag(0, &t, TAG_WAIT_MS);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, found,
        "No tag detected on " R0_LABEL " within timeout");
    TEST_ASSERT_GREATER_THAN_UINT8_MESSAGE(0, t.uid_len, "uid_len must be non-zero");
    strncpy(g_uid_r0, t.uid_str, IMB_NFC_UID_STR_LEN);
    printf("    UID: %s\n", g_uid_r0);
    fflush(stdout);
}

void test_scan_uid_str_is_uppercase_hex(void)
{
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, (int)strlen(g_uid_r0),
        "g_uid_r0 empty — run after test_scan_tag_found_inner_reader");
    for (int i = 0; g_uid_r0[i]; i++) {
        char c = g_uid_r0[i];
        int valid = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
        TEST_ASSERT_TRUE_MESSAGE(valid, "uid_str must be uppercase hex digits only");
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)strlen(g_uid_r0) % 2,
        "uid_str length must be even (2 hex chars per UID byte)");
}

void test_outer_reader_clear_while_tag_on_inner(void)
{
    imb_nfc_tag_t t;
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, imb_nfc_scan(1, &t),
        R1_LABEL " must not detect a tag placed on " R0_LABEL);
}

void test_find_by_uid_mismatch_returns_0(void)
{
    char wrong[IMB_NFC_UID_STR_LEN];
    strncpy(wrong, g_uid_r0, IMB_NFC_UID_STR_LEN);
    wrong[0] = (wrong[0] == 'A') ? 'B' : 'A';
    imb_nfc_tag_t t;
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, imb_nfc_find_by_uid(0, wrong, &t),
        "find_by_uid must return 0 on UID mismatch");
}

void test_find_by_uid_correct_uid_returns_1(void)
{
    imb_nfc_tag_t t;
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, imb_nfc_find_by_uid(0, g_uid_r0, &t),
        "find_by_uid must return 1 on matching UID");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(g_uid_r0, t.uid_str,
        "Returned tag UID must equal the target UID");
}

/* ═══════════════════════════════════════════════════════════════════════
 * GROUP 3 — Write + Read NDEF on inner reader
 * ═════════════════════════════════════════════════════════════════════*/

void test_write_ndef_succeeds_inner(void)
{
    imb_nfc_tag_t t;
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, wait_for_tag(0, &t, TAG_WAIT_MS),
        "No tag on " R0_LABEL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, imb_nfc_write_ndef(0, &t, "Torch"),
        "imb_nfc_write_ndef returned 0 — write failed");
}

void test_read_ndef_returns_written_name(void)
{
    imb_nfc_tag_t t;
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, wait_for_tag(0, &t, TAG_WAIT_MS),
        "No tag on " R0_LABEL);
    char name[IMB_NFC_NAME_MAX];
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, imb_nfc_read_ndef(0, &t, name, sizeof(name)),
        "imb_nfc_read_ndef returned 0");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Torch", name,
        "Read-back name must match what was written");
}

void test_write_ndef_overwrites_previous(void)
{
    imb_nfc_tag_t t;
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, wait_for_tag(0, &t, TAG_WAIT_MS),
        "No tag on " R0_LABEL);
    TEST_ASSERT_EQUAL_INT(1, imb_nfc_write_ndef(0, &t, "Lantern"));
    char name[IMB_NFC_NAME_MAX];
    TEST_ASSERT_EQUAL_INT(1, imb_nfc_read_ndef(0, &t, name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Lantern", name,
        "Overwrite failed — old name still present");
}

void test_read_ndef_truncates_to_name_max(void)
{
    imb_nfc_tag_t t;
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, wait_for_tag(0, &t, TAG_WAIT_MS),
        "No tag on " R0_LABEL);
    TEST_ASSERT_EQUAL_INT(1, imb_nfc_write_ndef(0, &t, "LongItemName"));
    char name[5];
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, imb_nfc_read_ndef(0, &t, name, sizeof(name)),
        "read_ndef must return 1 even when truncating");
    TEST_ASSERT_EQUAL_INT_MESSAGE('\0', name[4],
        "Truncated buffer must be null-terminated");
    TEST_ASSERT_EQUAL_INT_MESSAGE(4, (int)strlen(name),
        "Truncated name must be exactly 4 chars");
    imb_nfc_write_ndef(0, &t, "Torch"); /* restore known state */
}

/* ═══════════════════════════════════════════════════════════════════════
 * GROUP 4 — Outer reader: scan + write/read roundtrip
 * ═════════════════════════════════════════════════════════════════════*/

void test_scan_tag_found_outer_reader(void)
{
    imb_nfc_tag_t t;
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, wait_for_tag(1, &t, TAG_WAIT_MS),
        "No tag detected on " R1_LABEL " within timeout");
    TEST_ASSERT_GREATER_THAN_UINT8_MESSAGE(0, t.uid_len, "uid_len must be non-zero");
    strncpy(g_uid_r1, t.uid_str, IMB_NFC_UID_STR_LEN);
    printf("    UID: %s\n", g_uid_r1);
    fflush(stdout);
}

void test_inner_reader_clear_while_tag_on_outer(void)
{
    imb_nfc_tag_t t;
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, imb_nfc_scan(0, &t),
        R0_LABEL " must not bleed signal from a tag on " R1_LABEL);
}

void test_write_read_ndef_outer_reader(void)
{
    imb_nfc_tag_t t;
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, wait_for_tag(1, &t, TAG_WAIT_MS),
        "No tag on " R1_LABEL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, imb_nfc_write_ndef(1, &t, "Compass"),
        "NDEF write to outer reader failed");
    char name[IMB_NFC_NAME_MAX];
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, imb_nfc_read_ndef(1, &t, name, sizeof(name)),
        "NDEF read from outer reader failed");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Compass", name,
        "Read-back name on outer reader does not match 'Compass'");
}

/* ═══════════════════════════════════════════════════════════════════════
 * GROUP 5 — Movement state management (imb_nfc + imb_detector integration)
 *
 * Detector window: 3 s — you have 3 seconds to move the card between readers.
 *
 * Physical convention:
 *   INSERT  = outer reader (GPIO9) first, then inner reader (GPIO10)
 *   EXTRACT = inner reader (GPIO10) first, then outer reader (GPIO9)
 * ═════════════════════════════════════════════════════════════════════*/

void test_movement_insert_outer_then_inner(void)
{
    reset_detector_state();
    imb_detector_t det;
    imb_detector_init(&det, DETECTOR_WINDOW_MS, get_ms, on_det_event, NULL);

    int got = run_until_event(&det, MOVEMENT_WAIT_MS);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, got,
        "No detector event fired — did you touch both readers within 3 s?");
    TEST_ASSERT_EQUAL_INT_MESSAGE(IMB_INSERT, g_last_event.dir,
        "Expected IMB_INSERT (outer first, then inner)");
    printf("    Event: INSERT  UID: %s\n", g_last_event.uid);
    fflush(stdout);
}

void test_movement_extract_inner_then_outer(void)
{
    reset_detector_state();
    imb_detector_t det;
    imb_detector_init(&det, DETECTOR_WINDOW_MS, get_ms, on_det_event, NULL);

    int got = run_until_event(&det, MOVEMENT_WAIT_MS);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, got,
        "No detector event fired — did you touch both readers within 3 s?");
    TEST_ASSERT_EQUAL_INT_MESSAGE(IMB_EXTRACT, g_last_event.dir,
        "Expected IMB_EXTRACT (inner first, then outer)");
    printf("    Event: EXTRACT  UID: %s\n", g_last_event.uid);
    fflush(stdout);
}

void test_movement_ambiguous_card_stays_inner_only(void)
{
    reset_detector_state();
    imb_detector_t det;
    imb_detector_init(&det, DETECTOR_WINDOW_MS, get_ms, on_det_event, NULL);

    /* Give time for the card to be detected once, then the window to expire */
    int got = run_until_event(&det, AMBIGUOUS_WAIT_MS + DETECTOR_WINDOW_MS + 500);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, got,
        "No detector event fired — was a card placed on the inner reader?");
    TEST_ASSERT_EQUAL_INT_MESSAGE(IMB_AMBIGUOUS, g_last_event.dir,
        "Expected IMB_AMBIGUOUS (card seen on inner reader only, window expired)");
    printf("    Event: AMBIGUOUS  UID: %s\n", g_last_event.uid);
    fflush(stdout);
}

void test_movement_two_cards_both_readers_simultaneous(void)
{
    reset_detector_state();
    imb_detector_t det;
    imb_detector_init(&det, DETECTOR_WINDOW_MS, get_ms, on_det_event, NULL);

    /* Run until two events fire (one per card/reader) or timeout */
    for (int elapsed = 0; elapsed < MOVEMENT_WAIT_MS * 2; elapsed += POLL_MS) {
        scan_and_feed(&det);
        if (g_event_count >= 2) break;
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
    TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(2, g_event_count,
        "Expected 2 events (one per card) — were two different cards placed?");
    printf("    Events: %d fired\n", g_event_count);
    fflush(stdout);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Entry point
 * ═════════════════════════════════════════════════════════════════════*/

void app_main(void)
{
    printf("\n\n========================================\n");
    printf("  imb_nfc + imb_detector hardware test\n");
    printf("  MOSI=GPIO%-2d  MISO=GPIO%-2d  SCK=GPIO%-2d\n",
           NFC_MOSI, NFC_MISO, NFC_SCK);
    printf("  Inner reader: CS GPIO%d\n", NFC_CS0);
    printf("  Outer reader: CS GPIO%d\n", NFC_CS1);
    printf("========================================\n\n");

    imb_nfc_pn532_config_t cfg = {
        .mosi = NFC_MOSI,
        .miso = NFC_MISO,
        .sck  = NFC_SCK,
        .cs   = { NFC_CS0, NFC_CS1 },
    };
    imb_nfc_hal_t hal = imb_nfc_pn532_init(&cfg);
    imb_nfc_init(&hal);
    printf("PN532 init done — both readers awake.\n\n");

    /* ── Prewrite phase — before Unity ─────────────────────────────
     * Stamps both test cards with our NDEF format.
     * Raw page-4 bytes are printed automatically by imb_nfc_read_ndef
     * so any format mismatch on externally-written cards is visible.
     * If either prewrite fails the test halts here with a clear error.
     * ─────────────────────────────────────────────────────────────*/
    prompt("PREWRITE — place the 'card1' card on " R0_LABEL ".\n"
           ">>> Any existing NDEF will be shown, then overwritten with our format.");
    if (!prewrite_card(0, "card1", R0_LABEL)) {
        printf("\nFATAL: prewrite failed on " R0_LABEL
               " — check power, SPI wiring, and card type.\n");
        return;
    }

    prompt("PREWRITE — place the 'card2' card on " R1_LABEL ".\n"
           ">>> Any existing NDEF will be shown, then overwritten with our format.");
    if (!prewrite_card(1, "card2", R1_LABEL)) {
        printf("\nFATAL: prewrite failed on " R1_LABEL
               " — check power, SPI wiring, and card type.\n");
        return;
    }

    printf("Both cards prewritten successfully. Starting Unity tests.\n\n");

    UNITY_BEGIN();

    /* ── Group 0: verify prewritten cards are readable ─────────── */
    prompt("PLACE card1 on " R0_LABEL " and card2 on " R1_LABEL ".");
    RUN_TEST(test_read_card1_on_inner_reader);
    RUN_TEST(test_read_card2_on_outer_reader);

    /* ── Group 1: no tags ──────────────────────────────────────── */
    prompt("REMOVE all tags from BOTH readers.");
    RUN_TEST(test_scan_no_tag_inner_reader_returns_0);
    RUN_TEST(test_scan_no_tag_uid_str_empty);
    RUN_TEST(test_scan_no_tag_outer_reader_returns_0);
    RUN_TEST(test_find_by_uid_no_tag_returns_0);

    /* ── Group 2: tag on inner reader only ─────────────────────── */
    prompt("PLACE any tag on " R0_LABEL ". Keep " R1_LABEL " clear.");
    RUN_TEST(test_scan_tag_found_inner_reader);
    RUN_TEST(test_scan_uid_str_is_uppercase_hex);
    RUN_TEST(test_outer_reader_clear_while_tag_on_inner);
    RUN_TEST(test_find_by_uid_mismatch_returns_0);
    RUN_TEST(test_find_by_uid_correct_uid_returns_1);

    /* ── Group 3: write + read NDEF on inner reader ─────────────── */
    prompt("KEEP the same tag on " R0_LABEL ". Starting write/read tests.");
    RUN_TEST(test_write_ndef_succeeds_inner);
    RUN_TEST(test_read_ndef_returns_written_name);
    RUN_TEST(test_write_ndef_overwrites_previous);
    RUN_TEST(test_read_ndef_truncates_to_name_max);

    /* ── Group 4: outer reader ─────────────────────────────────── */
    prompt("REMOVE tag from " R0_LABEL ". PLACE any tag on " R1_LABEL ".");
    RUN_TEST(test_scan_tag_found_outer_reader);
    RUN_TEST(test_inner_reader_clear_while_tag_on_outer);
    RUN_TEST(test_write_read_ndef_outer_reader);

    /* ── Group 5: movement state management ───────────────────── */
    prompt(
        "MOVEMENT TESTS — detector window: 3 s per move.\n"
        ">>> Reader layout:  inner=GPIO" STRINGIFY(NFC_CS0) "   outer=GPIO" STRINGIFY(NFC_CS1) "\n"
        ">>> INSERT  = touch outer (GPIO" STRINGIFY(NFC_CS1) ") FIRST, then inner (GPIO" STRINGIFY(NFC_CS0) ") within 3 s\n"
        ">>> EXTRACT = touch inner (GPIO" STRINGIFY(NFC_CS0) ") FIRST, then outer (GPIO" STRINGIFY(NFC_CS1) ") within 3 s\n"
        ">>> Ready in 3 s...");

    prompt("TEST: INSERT — touch " R1_LABEL " first, then " R0_LABEL " within 3 s. Go!");
    RUN_TEST(test_movement_insert_outer_then_inner);

    prompt("TEST: EXTRACT — touch " R0_LABEL " first, then " R1_LABEL " within 3 s. Go!");
    RUN_TEST(test_movement_extract_inner_then_outer);

    prompt("TEST: AMBIGUOUS — place card on " R0_LABEL " and DO NOT move it to the outer reader.");
    RUN_TEST(test_movement_ambiguous_card_stays_inner_only);

    prompt(
        "TEST: TWO CARDS — place one card on " R0_LABEL " AND a DIFFERENT card on " R1_LABEL " simultaneously.\n"
        ">>> Hold both in place. Each card will generate its own event.");
    RUN_TEST(test_movement_two_cards_both_readers_simultaneous);

    UNITY_END();
    printf("\n>>> Test run complete. Remove all tags.\n");
}
