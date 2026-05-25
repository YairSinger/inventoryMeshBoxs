#include "unity.h"
#include "imb_detector.h"
#include <string.h>

/* ── test doubles ─────────────────────────────────────────────────────── */

static uint32_t mock_ms;
static uint32_t get_mock_ms(void) { return mock_ms; }

static imb_scan_event_t recorded_events[16];
static int               event_count;

static void on_event(const imb_scan_event_t *e, void *ctx)
{
    (void)ctx;
    if (event_count < 16) recorded_events[event_count++] = *e;
}

static void reset_stubs(void)
{
    mock_ms     = 0;
    event_count = 0;
    memset(recorded_events, 0, sizeof(recorded_events));
}

static imb_detector_t make_detector(void)
{
    imb_detector_t det;
    imb_detector_init(&det, 200, get_mock_ms, on_event, NULL);
    return det;
}

/* ── setUp / tearDown (called by Unity before/after each test) ────────── */

void setUp(void)    { reset_stubs(); }
void tearDown(void) {}

/* ── tests ────────────────────────────────────────────────────────────── */

void test_reader0_then_reader1_within_window_fires_INSERT(void)
{
    imb_detector_t det = make_detector();

    imb_detector_on_reader_event(&det, 0, "04A32F123456EF");
    mock_ms = 100;
    imb_detector_on_reader_event(&det, 1, "04A32F123456EF");

    TEST_ASSERT_EQUAL_INT(1, event_count);
    TEST_ASSERT_EQUAL_INT(IMB_INSERT, recorded_events[0].dir);
    TEST_ASSERT_EQUAL_STRING("04A32F123456EF", recorded_events[0].uid);
}

void test_reader1_then_reader0_within_window_fires_EXTRACT(void)
{
    imb_detector_t det = make_detector();

    imb_detector_on_reader_event(&det, 1, "04A32F123456EF");
    mock_ms = 100;
    imb_detector_on_reader_event(&det, 0, "04A32F123456EF");

    TEST_ASSERT_EQUAL_INT(1, event_count);
    TEST_ASSERT_EQUAL_INT(IMB_EXTRACT, recorded_events[0].dir);
}

void test_only_reader0_fires_window_expires_fires_AMBIGUOUS(void)
{
    imb_detector_t det = make_detector();

    imb_detector_on_reader_event(&det, 0, "04A32F123456EF");
    mock_ms = 201;
    imb_detector_tick(&det);

    TEST_ASSERT_EQUAL_INT(1, event_count);
    TEST_ASSERT_EQUAL_INT(IMB_AMBIGUOUS, recorded_events[0].dir);
}

void test_only_reader1_fires_window_expires_fires_AMBIGUOUS(void)
{
    imb_detector_t det = make_detector();

    imb_detector_on_reader_event(&det, 1, "04A32F123456EF");
    mock_ms = 201;
    imb_detector_tick(&det);

    TEST_ASSERT_EQUAL_INT(1, event_count);
    TEST_ASSERT_EQUAL_INT(IMB_AMBIGUOUS, recorded_events[0].dir);
}

void test_second_reader_fires_after_window_fires_AMBIGUOUS_then_new_pending(void)
{
    imb_detector_t det = make_detector();

    imb_detector_on_reader_event(&det, 0, "04A32F123456EF");
    mock_ms = 201;
    imb_detector_on_reader_event(&det, 1, "04A32F123456EF");

    /* first event → AMBIGUOUS (window expired), second starts a new pending */
    TEST_ASSERT_EQUAL_INT(1, event_count);
    TEST_ASSERT_EQUAL_INT(IMB_AMBIGUOUS, recorded_events[0].dir);
}
