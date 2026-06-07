#include "unity.h"
#include "imb_buzzer.h"
#include <stddef.h>

/* ── HAL spy ─────────────────────────────────────────────────────────────── */

typedef struct {
    uint32_t  tone_freqs[16];
    int       tone_count;
    int       silence_count;
    int       cancel_count;
    uint32_t  sched_ms[16];
    int       sched_count;
    void    (*sched_cb)(void *);
    void     *sched_arg;
} hal_spy_t;

static hal_spy_t g_hal;

static void spy_tone(uint32_t freq_hz)
{
    g_hal.tone_freqs[g_hal.tone_count++] = freq_hz;
}

static void spy_silence(void)
{
    g_hal.silence_count++;
}

static void spy_schedule_ms(uint32_t ms, void (*cb)(void *), void *arg)
{
    g_hal.sched_ms[g_hal.sched_count++] = ms;
    g_hal.sched_cb  = cb;
    g_hal.sched_arg = arg;
}

static void spy_cancel(void)
{
    g_hal.cancel_count++;
}

static void fire_timer(void)
{
    void (*cb)(void *) = g_hal.sched_cb;
    void  *arg         = g_hal.sched_arg;
    g_hal.sched_cb  = NULL;
    g_hal.sched_arg = NULL;
    if (cb) cb(arg);
}

static imb_buzzer_hal_t make_hal(void)
{
    imb_buzzer_hal_t h = {
        .tone        = spy_tone,
        .silence     = spy_silence,
        .schedule_ms = spy_schedule_ms,
        .cancel      = spy_cancel,
    };
    return h;
}

/* ── Fixture ─────────────────────────────────────────────────────────────── */

void setUp(void)
{
    hal_spy_t zero = {0};
    g_hal = zero;
    imb_buzzer_hal_t h = make_hal();
    imb_buzzer_init(&h);
}

void tearDown(void) {}

/* ── Tests ───────────────────────────────────────────────────────────────── */

void test_tag_placed_starts_tone_and_schedules_duration(void)
{
    imb_buzzer_play(IMB_BUZZ_TAG_PLACED);

    TEST_ASSERT_EQUAL_INT(1, g_hal.tone_count);
    TEST_ASSERT_EQUAL_UINT32(2700, g_hal.tone_freqs[0]);
    TEST_ASSERT_EQUAL_INT(1, g_hal.sched_count);
    TEST_ASSERT_EQUAL_UINT32(50, g_hal.sched_ms[0]);
    TEST_ASSERT_EQUAL_INT(0, g_hal.silence_count);
}

void test_tag_placed_timer_fires_silence_and_no_reschedule(void)
{
    imb_buzzer_play(IMB_BUZZ_TAG_PLACED);
    fire_timer();

    TEST_ASSERT_EQUAL_INT(1, g_hal.silence_count);
    TEST_ASSERT_EQUAL_INT(1, g_hal.sched_count); /* no extra schedule */
}

void test_item_removed_plays_two_beeps_with_gap(void)
{
    imb_buzzer_play(IMB_BUZZ_ITEM_REMOVED);

    /* first beep: tone(2700) + schedule(50ms) */
    TEST_ASSERT_EQUAL_INT(1, g_hal.tone_count);
    TEST_ASSERT_EQUAL_UINT32(2700, g_hal.tone_freqs[0]);
    TEST_ASSERT_EQUAL_UINT32(50, g_hal.sched_ms[0]);

    fire_timer(); /* duration done → silence + schedule gap */
    TEST_ASSERT_EQUAL_INT(1, g_hal.silence_count);
    TEST_ASSERT_EQUAL_UINT32(50, g_hal.sched_ms[1]);

    fire_timer(); /* gap done → second beep */
    TEST_ASSERT_EQUAL_INT(2, g_hal.tone_count);
    TEST_ASSERT_EQUAL_UINT32(2700, g_hal.tone_freqs[1]);
    TEST_ASSERT_EQUAL_UINT32(50, g_hal.sched_ms[2]);

    fire_timer(); /* duration done → silence, no more beeps */
    TEST_ASSERT_EQUAL_INT(2, g_hal.silence_count);
    TEST_ASSERT_EQUAL_INT(3, g_hal.sched_count); /* exactly 3 schedules total */
}

void test_unknown_tag_plays_long_beep(void)
{
    imb_buzzer_play(IMB_BUZZ_UNKNOWN_TAG);

    TEST_ASSERT_EQUAL_INT(1, g_hal.tone_count);
    TEST_ASSERT_EQUAL_UINT32(1800, g_hal.tone_freqs[0]);
    TEST_ASSERT_EQUAL_UINT32(300, g_hal.sched_ms[0]);

    fire_timer();
    TEST_ASSERT_EQUAL_INT(1, g_hal.silence_count);
    TEST_ASSERT_EQUAL_INT(1, g_hal.sched_count);
}

void test_error_plays_three_rapid_beeps(void)
{
    imb_buzzer_play(IMB_BUZZ_ERROR);

    TEST_ASSERT_EQUAL_UINT32(1000, g_hal.tone_freqs[0]);
    TEST_ASSERT_EQUAL_UINT32(80, g_hal.sched_ms[0]);

    fire_timer(); /* beep1 done → silence + gap */
    TEST_ASSERT_EQUAL_INT(1, g_hal.silence_count);
    TEST_ASSERT_EQUAL_UINT32(40, g_hal.sched_ms[1]);

    fire_timer(); /* gap → beep2 */
    TEST_ASSERT_EQUAL_INT(2, g_hal.tone_count);
    TEST_ASSERT_EQUAL_UINT32(80, g_hal.sched_ms[2]);

    fire_timer(); /* beep2 done → silence + gap */
    TEST_ASSERT_EQUAL_INT(2, g_hal.silence_count);
    TEST_ASSERT_EQUAL_UINT32(40, g_hal.sched_ms[3]);

    fire_timer(); /* gap → beep3 */
    TEST_ASSERT_EQUAL_INT(3, g_hal.tone_count);
    TEST_ASSERT_EQUAL_UINT32(80, g_hal.sched_ms[4]);

    fire_timer(); /* beep3 done → final silence */
    TEST_ASSERT_EQUAL_INT(3, g_hal.silence_count);
    TEST_ASSERT_EQUAL_INT(5, g_hal.sched_count);
}

void test_ble_connected_plays_rising_chirp(void)
{
    imb_buzzer_play(IMB_BUZZ_BLE_CONNECTED);

    TEST_ASSERT_EQUAL_UINT32(1500, g_hal.tone_freqs[0]);
    TEST_ASSERT_EQUAL_UINT32(80, g_hal.sched_ms[0]);

    fire_timer(); /* first tone done → silence + gap */
    TEST_ASSERT_EQUAL_INT(1, g_hal.silence_count);
    TEST_ASSERT_EQUAL_UINT32(30, g_hal.sched_ms[1]);

    fire_timer(); /* gap → second tone */
    TEST_ASSERT_EQUAL_UINT32(2500, g_hal.tone_freqs[1]);
    TEST_ASSERT_EQUAL_UINT32(80, g_hal.sched_ms[2]);

    fire_timer(); /* second tone done → silence */
    TEST_ASSERT_EQUAL_INT(2, g_hal.silence_count);
    TEST_ASSERT_EQUAL_INT(3, g_hal.sched_count);
}

void test_factory_reset_starts_continuous_tone_no_schedule(void)
{
    imb_buzzer_play(IMB_BUZZ_FACTORY_RESET);

    TEST_ASSERT_EQUAL_INT(1, g_hal.tone_count);
    TEST_ASSERT_EQUAL_UINT32(800, g_hal.tone_freqs[0]);
    TEST_ASSERT_EQUAL_INT(0, g_hal.sched_count); /* no auto-stop */
}

void test_buzzer_silence_calls_hal_silence_and_cancel(void)
{
    imb_buzzer_play(IMB_BUZZ_ITEM_REMOVED); /* in-progress pattern */
    imb_buzzer_silence();

    TEST_ASSERT_EQUAL_INT(1, g_hal.silence_count);
    TEST_ASSERT_EQUAL_INT(1, g_hal.cancel_count);
}
