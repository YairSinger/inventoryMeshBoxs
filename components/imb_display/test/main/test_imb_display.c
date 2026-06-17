#include "unity.h"
#include "imb_display.h"
#include <string.h>
#include <stdio.h>

/* ── HAL spy ─────────────────────────────────────────────────────────────── */

#define SPY_MAX_DRAWS 64

typedef struct {
    uint8_t row;
    uint8_t col;
    char    str[64];
} draw_call_t;

typedef struct {
    draw_call_t draws[SPY_MAX_DRAWS];
    int         draw_count;
    int         clear_count;
    int         cancel_count;
    uint32_t    sched_ms[16];
    int         sched_count;
    void      (*sched_cb)(void *);
    void       *sched_arg;
} hal_spy_t;

static hal_spy_t s_spy;

static void spy_draw_text(uint8_t row, uint8_t col, const char *str)
{
    int i = s_spy.draw_count++;
    s_spy.draws[i].row = row;
    s_spy.draws[i].col = col;
    strncpy(s_spy.draws[i].str, str, sizeof(s_spy.draws[i].str) - 1);
}

static void spy_clear(void)      { s_spy.clear_count++; }
static void spy_cancel(void)     { s_spy.cancel_count++; }

static void spy_schedule_ms(uint32_t ms, void (*cb)(void *), void *arg)
{
    int i = s_spy.sched_count++;
    s_spy.sched_ms[i] = ms;
    s_spy.sched_cb    = cb;
    s_spy.sched_arg   = arg;
}

static void fire_timer(void)
{
    void (*cb)(void *) = s_spy.sched_cb;
    void  *arg         = s_spy.sched_arg;
    s_spy.sched_cb  = NULL;
    s_spy.sched_arg = NULL;
    if (cb) cb(arg);
}

/* Returns true if any draw call on the given row contains substr. */
static bool row_contains(uint8_t row, const char *substr)
{
    for (int i = 0; i < s_spy.draw_count; i++) {
        if (s_spy.draws[i].row == row &&
            strstr(s_spy.draws[i].str, substr) != NULL) {
            return true;
        }
    }
    return false;
}

static imb_display_hal_t make_hal(void)
{
    return (imb_display_hal_t){
        .draw_text   = spy_draw_text,
        .clear       = spy_clear,
        .schedule_ms = spy_schedule_ms,
        .cancel      = spy_cancel,
    };
}

/* ── Fixture ─────────────────────────────────────────────────────────────── */

void setUp(void)
{
    memset(&s_spy, 0, sizeof(s_spy));
    imb_display_hal_t h = make_hal();
    imb_display_init(&h);
}

void tearDown(void) {}

/* ── Tests ───────────────────────────────────────────────────────────────── */

/* 1. FIELD_CHECK idle: box_name on row 0, mode on row 1,
      mesh peer count on row 2, phone status on row 3. */
void test_field_check_idle_draws_context(void)
{
    imb_display_ctx_t ctx = {
        .op_mode          = IMB_MODE_FIELD_CHECK,
        .mesh_peer_count  = 2,
        .phone_connected  = true,
    };
    strncpy(ctx.box_name, "AlphaBox", IMB_NAME_LEN - 1);
    strncpy(ctx.last4_mac, "A1B2", 4);

    imb_display_update_ctx(&ctx);

    TEST_ASSERT_TRUE(row_contains(0, "AlphaBox"));
    TEST_ASSERT_TRUE(row_contains(1, "FIELD"));
    TEST_ASSERT_TRUE(row_contains(2, "2"));
    TEST_ASSERT_TRUE(row_contains(3, "phone") || row_contains(3, "PHONE") ||
                     row_contains(3, "connected") || row_contains(3, "CONNECTED"));
}

/* 2. SETUP idle: row 0 = "SETUP", row 1 = last4_mac */
void test_setup_idle_draws_mode_and_mac(void)
{
    imb_display_ctx_t ctx = { .op_mode = IMB_MODE_SETUP };
    strncpy(ctx.last4_mac, "C3D4", 4);

    imb_display_update_ctx(&ctx);

    TEST_ASSERT_TRUE(row_contains(0, "SETUP"));
    TEST_ASSERT_TRUE(row_contains(1, "C3D4"));
}

/* 3. REGISTRATION idle: mode label on row 1, pending count on row 2 */
void test_registration_idle_draws_pending_count(void)
{
    imb_display_ctx_t ctx = {
        .op_mode           = IMB_MODE_REGISTRATION,
        .pending_tag_count = 3,
    };

    imb_display_update_ctx(&ctx);

    TEST_ASSERT_TRUE(row_contains(1, "REGISTRATION") || row_contains(1, "REG"));
    TEST_ASSERT_TRUE(row_contains(2, "3"));
}

/* 4. REGISTRATION_INCOMPLETE idle: error label on screen */
void test_registration_incomplete_idle_draws_error(void)
{
    imb_display_ctx_t ctx = { .op_mode = IMB_MODE_REGISTRATION_INCOMPLETE };

    imb_display_update_ctx(&ctx);

    TEST_ASSERT_TRUE(row_contains(0, "INCOMPLETE") || row_contains(1, "INCOMPLETE"));
}
/* 5. Named detection: direction + item name on screen, 5 s timer scheduled */
void test_named_detection_draws_event_and_schedules_5s(void)
{
    imb_display_ctx_t ctx = { .op_mode = IMB_MODE_FIELD_CHECK };
    strncpy(ctx.box_name, "Beta", IMB_NAME_LEN - 1);
    imb_display_update_ctx(&ctx);
    int clear_before = s_spy.clear_count;

    imb_display_on_detection(IMB_INSERT, "Flashlight");

    TEST_ASSERT_TRUE(row_contains(0, "INSERT") || row_contains(0, "IN") ||
                     row_contains(1, "INSERT") || row_contains(1, "IN"));
    TEST_ASSERT_TRUE(row_contains(0, "Flashlight") || row_contains(1, "Flashlight"));
    TEST_ASSERT_EQUAL_INT(1, s_spy.sched_count);
    TEST_ASSERT_EQUAL_UINT32(5000, s_spy.sched_ms[0]);
    TEST_ASSERT_GREATER_THAN_INT(clear_before, s_spy.clear_count);
}

/* 6. Timer fires → idle screen redrawn */
void test_event_timer_fires_returns_to_idle(void)
{
    imb_display_ctx_t ctx = { .op_mode = IMB_MODE_FIELD_CHECK };
    strncpy(ctx.box_name, "Gamma", IMB_NAME_LEN - 1);
    imb_display_update_ctx(&ctx);

    imb_display_on_detection(IMB_INSERT, "Lantern");
    int draws_after_event = s_spy.draw_count;

    fire_timer();

    TEST_ASSERT_GREATER_THAN_INT(draws_after_event, s_spy.draw_count);
    TEST_ASSERT_TRUE(row_contains(0, "Gamma"));
    /* No further timer scheduled */
    TEST_ASSERT_EQUAL_INT(1, s_spy.sched_count);
}

/* 7. EXTRACT direction shown differently from INSERT */
void test_extract_direction_shown_differently(void)
{
    imb_display_ctx_t ctx = { .op_mode = IMB_MODE_FIELD_CHECK };
    imb_display_update_ctx(&ctx);

    imb_display_on_detection(IMB_EXTRACT, "Compass");

    TEST_ASSERT_TRUE(row_contains(0, "EXTRACT") || row_contains(0, "OUT") ||
                     row_contains(1, "EXTRACT") || row_contains(1, "OUT"));
}
/* 8. Anonymous detection → UNKNOWN TAG error, no timer */
void test_anonymous_detection_draws_unknown_tag_error(void)
{
    imb_display_ctx_t ctx = { .op_mode = IMB_MODE_FIELD_CHECK };
    imb_display_update_ctx(&ctx);

    imb_display_on_detection(IMB_INSERT, NULL);

    TEST_ASSERT_TRUE(row_contains(0, "UNKNOWN") || row_contains(1, "UNKNOWN"));
    TEST_ASSERT_EQUAL_INT(0, s_spy.sched_count);  /* no timer */
}

/* 9. on_ambiguous(name) → AMBIGUOUS label + item name */
void test_ambiguous_named_draws_error_with_name(void)
{
    imb_display_ctx_t ctx = { .op_mode = IMB_MODE_FIELD_CHECK };
    imb_display_update_ctx(&ctx);

    imb_display_on_ambiguous("Compass");

    TEST_ASSERT_TRUE(row_contains(0, "AMBIGUOUS") || row_contains(1, "AMBIGUOUS"));
    TEST_ASSERT_TRUE(row_contains(0, "Compass") || row_contains(1, "Compass"));
    TEST_ASSERT_EQUAL_INT(0, s_spy.sched_count);
}

/* 10. on_ambiguous(NULL) → AMBIGUOUS + anonymous placeholder */
void test_ambiguous_anonymous_draws_error_anonymous_label(void)
{
    imb_display_ctx_t ctx = { .op_mode = IMB_MODE_FIELD_CHECK };
    imb_display_update_ctx(&ctx);

    imb_display_on_ambiguous(NULL);

    TEST_ASSERT_TRUE(row_contains(0, "AMBIGUOUS") || row_contains(1, "AMBIGUOUS"));
    TEST_ASSERT_EQUAL_INT(0, s_spy.sched_count);
}

/* 11. Error held: update_ctx while in error → screen text unchanged */
void test_error_held_update_ctx_does_not_redraw(void)
{
    imb_display_ctx_t ctx = { .op_mode = IMB_MODE_FIELD_CHECK };
    strncpy(ctx.box_name, "DeltaBox", IMB_NAME_LEN - 1);
    imb_display_update_ctx(&ctx);

    imb_display_on_ambiguous("Knife");
    int draws_after_error = s_spy.draw_count;

    /* update context — should NOT redraw because we're in error state */
    imb_display_ctx_t ctx2 = { .op_mode = IMB_MODE_FIELD_CHECK };
    strncpy(ctx2.box_name, "DeltaBox", IMB_NAME_LEN - 1);
    imb_display_update_ctx(&ctx2);

    TEST_ASSERT_EQUAL_INT(draws_after_error, s_spy.draw_count);
}
/* 12. Named detection clears error and shows event screen */
void test_named_detection_clears_error(void)
{
    imb_display_ctx_t ctx = { .op_mode = IMB_MODE_FIELD_CHECK };
    strncpy(ctx.box_name, "EpsilonBox", IMB_NAME_LEN - 1);
    imb_display_update_ctx(&ctx);
    imb_display_on_ambiguous("Knife");

    /* now a new named detection should clear the error */
    imb_display_on_detection(IMB_INSERT, "Flashlight");

    TEST_ASSERT_TRUE(row_contains(0, "INSERT") || row_contains(0, "IN") ||
                     row_contains(1, "INSERT") || row_contains(1, "IN"));
    TEST_ASSERT_TRUE(row_contains(0, "Flashlight") || row_contains(1, "Flashlight"));
    TEST_ASSERT_EQUAL_INT(1, s_spy.sched_count); /* 5 s timer now set */
}

/* 13. Report cycles MISSING → FOREIGN → AMBIGUOUS at 2 s each, then idle */
void test_report_cycles_missing_foreign_ambiguous_then_idle(void)
{
    imb_display_ctx_t ctx = { .op_mode = IMB_MODE_FIELD_CHECK };
    strncpy(ctx.box_name, "ZetaBox", IMB_NAME_LEN - 1);
    imb_display_update_ctx(&ctx);

    imb_display_report_t rpt = {
        .missing_count   = 1,
        .foreign_count   = 1,
        .ambiguous_count = 1,
        .total_count     = 3,
    };
    strncpy(rpt.missing[0],   "Lantern",  IMB_NAME_LEN - 1);
    strncpy(rpt.foreign[0],   "Wrench",   IMB_NAME_LEN - 1);
    strncpy(rpt.ambiguous[0], "Map",      IMB_NAME_LEN - 1);

    imb_display_on_report(&rpt);

    /* first item: MISSING + "Lantern", 2 s timer */
    TEST_ASSERT_TRUE(row_contains(0, "MISSING") || row_contains(1, "MISSING"));
    TEST_ASSERT_TRUE(row_contains(0, "Lantern") || row_contains(1, "Lantern"));
    TEST_ASSERT_EQUAL_UINT32(2000, s_spy.sched_ms[0]);

    fire_timer(); /* → FOREIGN item */
    TEST_ASSERT_TRUE(row_contains(0, "FOREIGN") || row_contains(1, "FOREIGN"));
    TEST_ASSERT_TRUE(row_contains(0, "Wrench") || row_contains(1, "Wrench"));
    TEST_ASSERT_EQUAL_UINT32(2000, s_spy.sched_ms[1]);

    fire_timer(); /* → AMBIGUOUS item */
    TEST_ASSERT_TRUE(row_contains(0, "AMBIGUOUS") || row_contains(1, "AMBIGUOUS"));
    TEST_ASSERT_TRUE(row_contains(0, "Map") || row_contains(1, "Map"));
    TEST_ASSERT_EQUAL_UINT32(2000, s_spy.sched_ms[2]);

    fire_timer(); /* → back to idle */
    TEST_ASSERT_TRUE(row_contains(0, "ZetaBox"));
    TEST_ASSERT_EQUAL_INT(3, s_spy.sched_count); /* exactly 3 timers fired */
}

/* 14. Empty report returns to idle immediately, no timer */
void test_empty_report_returns_to_idle_immediately(void)
{
    imb_display_ctx_t ctx = { .op_mode = IMB_MODE_FIELD_CHECK };
    strncpy(ctx.box_name, "EtaBox", IMB_NAME_LEN - 1);
    imb_display_update_ctx(&ctx);

    imb_display_report_t rpt = { 0 };
    imb_display_on_report(&rpt);

    TEST_ASSERT_TRUE(row_contains(0, "EtaBox"));
    TEST_ASSERT_EQUAL_INT(0, s_spy.sched_count); /* no timer */
}
