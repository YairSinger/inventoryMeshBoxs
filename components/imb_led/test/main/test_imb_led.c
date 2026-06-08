#include "unity.h"
#include "imb_led.h"
#include <stddef.h>
#include <string.h>

/* ── HAL spy ─────────────────────────────────────────────────────────────── */

typedef struct {
    uint8_t  r[32], g[32], b[32];
    int      color_count;
    int      cancel_count;
    uint32_t sched_ms[32];
    int      sched_count;
    void   (*sched_cb)(void *);
    void    *sched_arg;
} hal_spy_t;

static hal_spy_t s_hal;

static void spy_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    int i = s_hal.color_count++;
    s_hal.r[i] = r;
    s_hal.g[i] = g;
    s_hal.b[i] = b;
}

static void spy_schedule_ms(uint32_t ms, void (*cb)(void *), void *arg)
{
    s_hal.sched_ms[s_hal.sched_count++] = ms;
    s_hal.sched_cb  = cb;
    s_hal.sched_arg = arg;
}

static void spy_cancel(void)
{
    s_hal.cancel_count++;
}

static void fire_timer(void)
{
    void (*cb)(void *) = s_hal.sched_cb;
    void  *arg         = s_hal.sched_arg;
    s_hal.sched_cb  = NULL;
    s_hal.sched_arg = NULL;
    if (cb) cb(arg);
}

static imb_led_hal_t make_hal(void)
{
    imb_led_hal_t h = {
        .set_color   = spy_set_color,
        .schedule_ms = spy_schedule_ms,
        .cancel      = spy_cancel,
    };
    return h;
}

/* ── Fixture ─────────────────────────────────────────────────────────────── */

void setUp(void)
{
    memset(&s_hal, 0, sizeof(s_hal));
    imb_led_hal_t h = make_hal();
    imb_led_init(&h);
}

void tearDown(void) {}

/* ── Tests ───────────────────────────────────────────────────────────────── */

void test_tag_insert_sets_green_and_schedules_duration(void)
{
    imb_led_play(IMB_LED_TAG_INSERT);

    TEST_ASSERT_EQUAL_INT(1, s_hal.color_count);
    TEST_ASSERT_EQUAL_UINT8(0,   s_hal.r[0]);
    TEST_ASSERT_EQUAL_UINT8(255, s_hal.g[0]);
    TEST_ASSERT_EQUAL_UINT8(0,   s_hal.b[0]);
    TEST_ASSERT_EQUAL_INT(1, s_hal.sched_count);
    TEST_ASSERT_EQUAL_UINT32(100, s_hal.sched_ms[0]);
}

void test_tag_insert_timer_fires_turns_off(void)
{
    imb_led_play(IMB_LED_TAG_INSERT);
    fire_timer();

    /* set_color(0,0,0) after duration, no further schedule */
    TEST_ASSERT_EQUAL_INT(2, s_hal.color_count);
    TEST_ASSERT_EQUAL_UINT8(0, s_hal.r[1]);
    TEST_ASSERT_EQUAL_UINT8(0, s_hal.g[1]);
    TEST_ASSERT_EQUAL_UINT8(0, s_hal.b[1]);
    TEST_ASSERT_EQUAL_INT(1, s_hal.sched_count);
}

void test_reg_fail_double_flash_with_gap(void)
{
    imb_led_play(IMB_LED_REG_FAIL);

    /* step 0: red on, schedule 100ms */
    TEST_ASSERT_EQUAL_UINT8(255, s_hal.r[0]);
    TEST_ASSERT_EQUAL_UINT32(100, s_hal.sched_ms[0]);

    fire_timer(); /* duration done → gap: set black, schedule 100ms */
    TEST_ASSERT_EQUAL_INT(2, s_hal.color_count);
    TEST_ASSERT_EQUAL_UINT8(0, s_hal.r[1]);
    TEST_ASSERT_EQUAL_UINT32(100, s_hal.sched_ms[1]);

    fire_timer(); /* gap done → step 1: red on, schedule 100ms */
    TEST_ASSERT_EQUAL_INT(3, s_hal.color_count);
    TEST_ASSERT_EQUAL_UINT8(255, s_hal.r[2]);
    TEST_ASSERT_EQUAL_UINT32(100, s_hal.sched_ms[2]);

    fire_timer(); /* step 1 duration done, no gap → off, done */
    TEST_ASSERT_EQUAL_INT(4, s_hal.color_count);
    TEST_ASSERT_EQUAL_UINT8(0, s_hal.r[3]);
    TEST_ASSERT_EQUAL_INT(3, s_hal.sched_count);  /* exactly 3 schedules */
}

void test_ble_idle_repeats_dim_white_pulse(void)
{
    imb_led_play(IMB_LED_BLE_IDLE);

    /* first pulse: dim white, schedule 200ms */
    TEST_ASSERT_EQUAL_UINT8(30, s_hal.r[0]);
    TEST_ASSERT_EQUAL_UINT8(30, s_hal.g[0]);
    TEST_ASSERT_EQUAL_UINT8(30, s_hal.b[0]);
    TEST_ASSERT_EQUAL_UINT32(200, s_hal.sched_ms[0]);

    fire_timer(); /* duration done → gap=2800ms: set black */
    TEST_ASSERT_EQUAL_UINT8(0, s_hal.r[1]);
    TEST_ASSERT_EQUAL_UINT32(2800, s_hal.sched_ms[1]);

    fire_timer(); /* gap done → repeat: dim white again */
    TEST_ASSERT_EQUAL_UINT8(30, s_hal.r[2]);
    TEST_ASSERT_EQUAL_UINT8(30, s_hal.g[2]);
    TEST_ASSERT_EQUAL_UINT8(30, s_hal.b[2]);
    TEST_ASSERT_EQUAL_UINT32(200, s_hal.sched_ms[2]);
}

void test_sleep_sets_off_immediately_no_schedule(void)
{
    imb_led_play(IMB_LED_SLEEP);

    TEST_ASSERT_EQUAL_INT(1, s_hal.color_count);
    TEST_ASSERT_EQUAL_UINT8(0, s_hal.r[0]);
    TEST_ASSERT_EQUAL_UINT8(0, s_hal.g[0]);
    TEST_ASSERT_EQUAL_UINT8(0, s_hal.b[0]);
    TEST_ASSERT_EQUAL_INT(0, s_hal.sched_count);
}

void test_stop_cancels_and_turns_off(void)
{
    imb_led_play(IMB_LED_FACTORY_RESET);  /* continuous */
    int color_before = s_hal.color_count;
    int cancel_before = s_hal.cancel_count;

    imb_led_stop();

    TEST_ASSERT_GREATER_THAN_INT(cancel_before, s_hal.cancel_count);
    TEST_ASSERT_GREATER_THAN_INT(color_before, s_hal.color_count);
    /* last color call must be black */
    int last = s_hal.color_count - 1;
    TEST_ASSERT_EQUAL_UINT8(0, s_hal.r[last]);
    TEST_ASSERT_EQUAL_UINT8(0, s_hal.g[last]);
    TEST_ASSERT_EQUAL_UINT8(0, s_hal.b[last]);
}

void test_breathing_no_dark_flash_between_steps(void)
{
    /* Breathing steps have gap_ms=0; advancing to next step should NOT
       insert a black frame between consecutive brightness levels. */
    imb_led_play(IMB_LED_MESH_DISC);

    /* step 0: {0,0,32} */
    TEST_ASSERT_EQUAL_UINT8(32, s_hal.b[0]);

    fire_timer(); /* duration done, gap=0 → advance directly to step 1 */

    /* step 1: {0,0,64} — no black frame in between */
    TEST_ASSERT_EQUAL_INT(2, s_hal.color_count);
    TEST_ASSERT_EQUAL_UINT8(64, s_hal.b[1]);
}
