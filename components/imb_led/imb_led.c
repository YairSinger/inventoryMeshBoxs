#include "imb_led.h"
#include <stddef.h>

typedef struct {
    uint8_t  r, g, b;
    uint32_t duration_ms;
    uint32_t gap_ms;   /* > 0 → LED goes dark for gap_ms before next step */
} led_step_t;

/* ── Pattern table ───────────────────────────────────────────────────────── */

static const led_step_t k_tag_insert[]  = { {0, 255, 0,   100, 0} };
static const led_step_t k_tag_extract[] = { {255, 0, 0,   100, 0} };
static const led_step_t k_ambiguous[]   = { {255, 180, 0, 100, 0} };
static const led_step_t k_reg_pass[]    = { {0, 255, 0,  2000, 0} };
static const led_step_t k_reg_fail[]    = {
    {255, 0, 0, 100, 100},
    {255, 0, 0, 100, 0},
};

/* Breathing: ramp up then down; last {0,0,0} step is the dark phase */
static const led_step_t k_mesh_disc[] = {
    {0, 0,  32, 200, 0}, {0, 0,  64, 200, 0},
    {0, 0, 128, 200, 0}, {0, 0, 255, 200, 0},
    {0, 0, 128, 200, 0}, {0, 0,  64, 200, 0},
    {0, 0,  32, 200, 0}, {0, 0,   0, 200, 0},
};
static const led_step_t k_factory_hold[] = {
    {32,  0, 0, 200, 0}, { 64, 0, 0, 200, 0},
    {128, 0, 0, 200, 0}, {255, 0, 0, 200, 0},
    {128, 0, 0, 200, 0}, { 64, 0, 0, 200, 0},
    {32,  0, 0, 200, 0}, {  0, 0, 0, 200, 0},
};
static const led_step_t k_ble_idle[]       = { {30, 30, 30, 200, 2800} };
static const led_step_t k_factory_reset[]  = { {255, 0, 0, 100, 100} };

typedef struct {
    const led_step_t *steps;
    int               n_steps;
    int               cur;
    int               in_gap;
    int               repeats;
} led_state_t;

static imb_led_hal_t g_hal;
static led_state_t   g_state;

static void on_duration_done(void *arg);
static void on_gap_done(void *arg);

static void advance_step(void);

static void start_step(int i)
{
    g_state.cur    = i;
    g_state.in_gap = 0;
    g_hal.set_color(g_state.steps[i].r, g_state.steps[i].g, g_state.steps[i].b);
    g_hal.schedule_ms(g_state.steps[i].duration_ms, on_duration_done, NULL);
}

static void on_duration_done(void *arg)
{
    (void)arg;
    int cur = g_state.cur;
    if (g_state.steps[cur].gap_ms > 0) {
        g_state.in_gap = 1;
        g_hal.set_color(0, 0, 0);
        g_hal.schedule_ms(g_state.steps[cur].gap_ms, on_gap_done, NULL);
    } else {
        advance_step();
    }
}

static void on_gap_done(void *arg)
{
    (void)arg;
    advance_step();
}

static void advance_step(void)
{
    int next = g_state.cur + 1;
    if (next >= g_state.n_steps) {
        if (g_state.repeats) {
            start_step(0);
        } else {
            g_hal.set_color(0, 0, 0);
            g_state.steps = NULL;
        }
    } else {
        start_step(next);
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void imb_led_init(const imb_led_hal_t *hal)
{
    g_hal   = *hal;
    g_state = (led_state_t){0};
}

void imb_led_play(imb_led_pattern_e pattern)
{
    g_hal.cancel();
    g_state = (led_state_t){0};

    switch (pattern) {
    case IMB_LED_TAG_INSERT:
        g_state.steps   = k_tag_insert;
        g_state.n_steps = 1;
        break;
    case IMB_LED_TAG_EXTRACT:
        g_state.steps   = k_tag_extract;
        g_state.n_steps = 1;
        break;
    case IMB_LED_AMBIGUOUS:
        g_state.steps   = k_ambiguous;
        g_state.n_steps = 1;
        break;
    case IMB_LED_REG_PASS:
        g_state.steps   = k_reg_pass;
        g_state.n_steps = 1;
        break;
    case IMB_LED_REG_FAIL:
        g_state.steps   = k_reg_fail;
        g_state.n_steps = 2;
        break;
    case IMB_LED_MESH_DISC:
        g_state.steps   = k_mesh_disc;
        g_state.n_steps = 8;
        g_state.repeats = 1;
        break;
    case IMB_LED_BLE_IDLE:
        g_state.steps   = k_ble_idle;
        g_state.n_steps = 1;
        g_state.repeats = 1;
        break;
    case IMB_LED_FACTORY_HOLD:
        g_state.steps   = k_factory_hold;
        g_state.n_steps = 8;
        g_state.repeats = 1;
        break;
    case IMB_LED_FACTORY_RESET:
        g_state.steps   = k_factory_reset;
        g_state.n_steps = 1;
        g_state.repeats = 1;
        break;
    case IMB_LED_SLEEP:
        g_hal.set_color(0, 0, 0);
        return;
    }
    start_step(0);
}

void imb_led_stop(void)
{
    g_hal.cancel();
    g_hal.set_color(0, 0, 0);
    g_state = (led_state_t){0};
}
