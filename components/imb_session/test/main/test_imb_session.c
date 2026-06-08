#include "unity.h"
#include "imb_session.h"
#include <string.h>

static imb_session_t s;

void setUp(void)    { imb_session_init(&s); }
void tearDown(void) {}

static imb_scan_event_t make_event(imb_direction_e dir, const char *uid)
{
    imb_scan_event_t e;
    e.dir = dir;
    strncpy(e.uid, uid, sizeof(e.uid) - 1);
    e.uid[sizeof(e.uid) - 1] = '\0';
    return e;
}

/* ── tests ─────────────────────────────────────────────────────────────── */

void test_session_reset_clears_all_sets(void)
{
    imb_session_apply(&s, &(imb_scan_event_t){ .dir = IMB_INSERT,    .uid = "04A32F123456EF" });
    imb_session_apply(&s, &(imb_scan_event_t){ .dir = IMB_AMBIGUOUS, .uid = "BBBBBBBBBBBB02" });

    imb_session_reset(&s);

    imb_entry_u out[IMB_REGISTRY_MAX_ITEMS];
    TEST_ASSERT_EQUAL_UINT16(0, imb_session_get_present(&s,   out, IMB_REGISTRY_MAX_ITEMS));
    TEST_ASSERT_EQUAL_UINT16(0, imb_session_get_ambiguous(&s, out, IMB_REGISTRY_MAX_ITEMS));
}

void test_insert_then_extract_item_not_in_present_set(void)
{
    imb_session_apply(&s, &(imb_scan_event_t){ .dir = IMB_INSERT,  .uid = "04A32F123456EF" });
    imb_session_apply(&s, &(imb_scan_event_t){ .dir = IMB_EXTRACT, .uid = "04A32F123456EF" });

    imb_entry_u out[IMB_REGISTRY_MAX_ITEMS];
    TEST_ASSERT_EQUAL_UINT16(0, imb_session_get_present(&s, out, IMB_REGISTRY_MAX_ITEMS));
}

void test_ambiguous_event_goes_to_ambiguous_set_not_present(void)
{
    imb_scan_event_t e = make_event(IMB_AMBIGUOUS, "04A32F123456EF");
    imb_session_apply(&s, &e);

    imb_entry_u out[IMB_REGISTRY_MAX_ITEMS];
    TEST_ASSERT_EQUAL_UINT16(0, imb_session_get_present(&s, out, IMB_REGISTRY_MAX_ITEMS));

    uint16_t ambi_count = imb_session_get_ambiguous(&s, out, IMB_REGISTRY_MAX_ITEMS);
    TEST_ASSERT_EQUAL_UINT16(1, ambi_count);
    TEST_ASSERT_EQUAL_STRING("04A32F123456EF", out[0].uid);
}

void test_extract_event_removes_item_from_present_set(void)
{
    imb_scan_event_t ins  = make_event(IMB_INSERT,  "04A32F123456EF");
    imb_scan_event_t extr = make_event(IMB_EXTRACT, "04A32F123456EF");
    imb_session_apply(&s, &ins);
    imb_session_apply(&s, &extr);

    imb_entry_u out[IMB_REGISTRY_MAX_ITEMS];
    uint16_t count = imb_session_get_present(&s, out, IMB_REGISTRY_MAX_ITEMS);
    TEST_ASSERT_EQUAL_UINT16(0, count);
}

void test_insert_event_adds_item_to_present_set(void)
{
    imb_scan_event_t e = make_event(IMB_INSERT, "04A32F123456EF");
    imb_session_apply(&s, &e);

    imb_entry_u out[IMB_REGISTRY_MAX_ITEMS];
    uint16_t count = imb_session_get_present(&s, out, IMB_REGISTRY_MAX_ITEMS);

    TEST_ASSERT_EQUAL_UINT16(1, count);
    TEST_ASSERT_EQUAL_STRING("04A32F123456EF", out[0].uid);
}

void test_double_insert_moves_to_ambiguous(void)
{
    imb_scan_event_t e = make_event(IMB_INSERT, "04A32F123456EF");
    imb_session_apply(&s, &e);
    imb_session_apply(&s, &e);  /* second INSERT while already present */

    imb_entry_u out[IMB_REGISTRY_MAX_ITEMS];
    TEST_ASSERT_EQUAL_UINT16(0, imb_session_get_present(&s,   out, IMB_REGISTRY_MAX_ITEMS));
    TEST_ASSERT_EQUAL_UINT16(1, imb_session_get_ambiguous(&s, out, IMB_REGISTRY_MAX_ITEMS));
    TEST_ASSERT_EQUAL_STRING("04A32F123456EF", out[0].uid);
}

void test_orphan_extract_goes_to_ambiguous(void)
{
    /* EXTRACT with no prior INSERT */
    imb_scan_event_t e = make_event(IMB_EXTRACT, "04A32F123456EF");
    imb_session_apply(&s, &e);

    imb_entry_u out[IMB_REGISTRY_MAX_ITEMS];
    TEST_ASSERT_EQUAL_UINT16(0, imb_session_get_present(&s,   out, IMB_REGISTRY_MAX_ITEMS));
    TEST_ASSERT_EQUAL_UINT16(1, imb_session_get_ambiguous(&s, out, IMB_REGISTRY_MAX_ITEMS));
    TEST_ASSERT_EQUAL_STRING("04A32F123456EF", out[0].uid);
}
