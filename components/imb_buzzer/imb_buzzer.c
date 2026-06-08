#include "imb_buzzer.h"
#include <stddef.h>

/* ── Step descriptor ─────────────────────────────────────────────────────── */

typedef struct {
    uint32_t freq_hz;
    uint32_t duration_ms;
    uint32_t gap_ms;
} buzz_step_t;

/* ── Pattern table ───────────────────────────────────────────────────────── */

static const buzz_step_t k_tag_placed[] = {
    { 2700, 50, 0 },
};

static const buzz_step_t k_item_removed[] = {
    { 2700, 50, 50 },
    { 2700, 50, 0  },
};

static const buzz_step_t k_unknown_tag[] = {
    { 1800, 300, 0 },
};

static const buzz_step_t k_error[] = {
    { 1000, 80, 40 },
    { 1000, 80, 40 },
    { 1000, 80, 0  },
};

static const buzz_step_t k_ble_connected[] = {
    { 1500, 80, 30 },
    { 2500, 80, 0  },
};

/* ── State ───────────────────────────────────────────────────────────────── */

typedef struct {
    const buzz_step_t *steps;
    int                n_steps;
    int                cur;
    int                in_gap;    /* 1 = currently in gap between beeps */
    int                continuous; /* 1 = FACTORY_RESET tone, no auto-stop */
} buzzer_state_t;

static imb_buzzer_hal_t g_hal;
static buzzer_state_t   g_state;

/* ── Forward declarations ────────────────────────────────────────────────── */

static void on_duration_done(void *arg);
static void on_gap_done(void *arg);

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static void start_step(int i)
{
    g_state.cur    = i;
    g_state.in_gap = 0;
    g_hal.tone(g_state.steps[i].freq_hz);
    g_hal.schedule_ms(g_state.steps[i].duration_ms, on_duration_done, NULL);
}

static void on_duration_done(void *arg)
{
    (void)arg;
    g_hal.silence();
    int cur = g_state.cur;
    if (g_state.steps[cur].gap_ms > 0) {
        g_state.in_gap = 1;
        g_hal.schedule_ms(g_state.steps[cur].gap_ms, on_gap_done, NULL);
    } else {
        g_state.steps = NULL;   /* pattern complete → idle */
    }
}

static void on_gap_done(void *arg)
{
    (void)arg;
    start_step(g_state.cur + 1);
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void imb_buzzer_init(const imb_buzzer_hal_t *hal)
{
    g_hal   = *hal;
    g_state = (buzzer_state_t){0};
}

void imb_buzzer_play(imb_buzzer_pattern_e pattern)
{
    switch (pattern) {
    case IMB_BUZZ_TAG_PLACED:
        g_state.steps   = k_tag_placed;
        g_state.n_steps = 1;
        break;
    case IMB_BUZZ_ITEM_REMOVED:
        g_state.steps   = k_item_removed;
        g_state.n_steps = 2;
        break;
    case IMB_BUZZ_UNKNOWN_TAG:
        g_state.steps   = k_unknown_tag;
        g_state.n_steps = 1;
        break;
    case IMB_BUZZ_ERROR:
        g_state.steps   = k_error;
        g_state.n_steps = 3;
        break;
    case IMB_BUZZ_BLE_CONNECTED:
        g_state.steps   = k_ble_connected;
        g_state.n_steps = 2;
        break;
    case IMB_BUZZ_FACTORY_RESET:
        g_state.continuous = 1;
        g_hal.tone(800);
        return;
    }
    start_step(0);
}

void imb_buzzer_silence(void)
{
    g_hal.cancel();
    g_hal.silence();
    g_state.steps      = NULL;
    g_state.continuous = 0;
}

int imb_buzzer_is_idle(void)
{
    return (g_state.steps == NULL && !g_state.continuous);
}
