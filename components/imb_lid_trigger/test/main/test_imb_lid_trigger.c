#include "unity.h"
#include "imb_lid_trigger.h"
#include <string.h>

typedef struct {
    imb_lid_state_e state;
    uint32_t now_ms;
    int read_count;
} hal_spy_t;

static hal_spy_t s_hal;
static imb_lid_trigger_t s_trigger;

static imb_lid_state_e spy_read_state(void *ctx)
{
    hal_spy_t *hal = (hal_spy_t *)ctx;
    hal->read_count++;
    return hal->state;
}

static uint32_t spy_now_ms(void *ctx)
{
    hal_spy_t *hal = (hal_spy_t *)ctx;
    return hal->now_ms;
}

static imb_lid_trigger_hal_t make_hal(void)
{
    imb_lid_trigger_hal_t hal = {
        .read_state = spy_read_state,
        .now_ms = spy_now_ms,
        .ctx = &s_hal,
    };
    return hal;
}

void setUp(void)
{
    memset(&s_hal, 0, sizeof(s_hal));
    memset(&s_trigger, 0, sizeof(s_trigger));
}

void tearDown(void) {}

void test_boot_open_seeds_stable_state_immediately(void)
{
    s_hal.state = IMB_LID_OPEN;
    imb_lid_trigger_hal_t hal = make_hal();

    imb_lid_trigger_init(&s_trigger, &hal);

    TEST_ASSERT_EQUAL(IMB_LID_OPEN, imb_lid_trigger_get_state(&s_trigger));
    TEST_ASSERT_EQUAL_INT(1, s_hal.read_count);
}

void test_boot_closed_seeds_stable_state_immediately(void)
{
    s_hal.state = IMB_LID_CLOSED;
    imb_lid_trigger_hal_t hal = make_hal();

    imb_lid_trigger_init(&s_trigger, &hal);

    TEST_ASSERT_EQUAL(IMB_LID_CLOSED, imb_lid_trigger_get_state(&s_trigger));
    TEST_ASSERT_EQUAL_INT(1, s_hal.read_count);
}

void test_open_edge_publishes_after_debounce_window(void)
{
    s_hal.state = IMB_LID_CLOSED;
    s_hal.now_ms = 1000;
    imb_lid_trigger_hal_t hal = make_hal();
    imb_lid_trigger_init(&s_trigger, &hal);

    s_hal.state = IMB_LID_OPEN;
    imb_lid_state_e changed_state = IMB_LID_CLOSED;
    TEST_ASSERT_FALSE(imb_lid_trigger_poll(&s_trigger, &changed_state));

    s_hal.now_ms = 1049;
    TEST_ASSERT_FALSE(imb_lid_trigger_poll(&s_trigger, &changed_state));
    TEST_ASSERT_EQUAL(IMB_LID_CLOSED, imb_lid_trigger_get_state(&s_trigger));

    s_hal.now_ms = 1050;
    TEST_ASSERT_TRUE(imb_lid_trigger_poll(&s_trigger, &changed_state));
    TEST_ASSERT_EQUAL(IMB_LID_OPEN, changed_state);
    TEST_ASSERT_EQUAL(IMB_LID_OPEN, imb_lid_trigger_get_state(&s_trigger));
}

void test_bounce_back_to_stable_state_is_ignored(void)
{
    s_hal.state = IMB_LID_CLOSED;
    s_hal.now_ms = 2000;
    imb_lid_trigger_hal_t hal = make_hal();
    imb_lid_trigger_init(&s_trigger, &hal);

    imb_lid_state_e changed_state = IMB_LID_CLOSED;
    s_hal.state = IMB_LID_OPEN;
    TEST_ASSERT_FALSE(imb_lid_trigger_poll(&s_trigger, &changed_state));

    s_hal.state = IMB_LID_CLOSED;
    s_hal.now_ms = 2025;
    TEST_ASSERT_FALSE(imb_lid_trigger_poll(&s_trigger, &changed_state));
    TEST_ASSERT_EQUAL(IMB_LID_CLOSED, imb_lid_trigger_get_state(&s_trigger));

    s_hal.state = IMB_LID_OPEN;
    s_hal.now_ms = 2080;
    TEST_ASSERT_FALSE(imb_lid_trigger_poll(&s_trigger, &changed_state));

    s_hal.now_ms = 2130;
    TEST_ASSERT_TRUE(imb_lid_trigger_poll(&s_trigger, &changed_state));
    TEST_ASSERT_EQUAL(IMB_LID_OPEN, changed_state);
}

void test_close_edge_publishes_after_debounce_window(void)
{
    s_hal.state = IMB_LID_OPEN;
    s_hal.now_ms = 3000;
    imb_lid_trigger_hal_t hal = make_hal();
    imb_lid_trigger_init(&s_trigger, &hal);

    s_hal.state = IMB_LID_CLOSED;
    imb_lid_state_e changed_state = IMB_LID_OPEN;
    TEST_ASSERT_FALSE(imb_lid_trigger_poll(&s_trigger, &changed_state));

    s_hal.now_ms = 3050;
    TEST_ASSERT_TRUE(imb_lid_trigger_poll(&s_trigger, &changed_state));
    TEST_ASSERT_EQUAL(IMB_LID_CLOSED, changed_state);
    TEST_ASSERT_EQUAL(IMB_LID_CLOSED, imb_lid_trigger_get_state(&s_trigger));
}
