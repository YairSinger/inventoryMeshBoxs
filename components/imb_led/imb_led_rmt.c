#include "imb_led_rmt.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#define LED_GPIO          48
#define LED_RMT_RES_HZ    10000000   /* 10 MHz → 100 ns per tick */

/* WS2812B timing (tolerances per datasheet):
   T0H ~400 ns = 4 ticks,  T0L ~850 ns = 9 ticks (relaxed to 8)
   T1H ~800 ns = 8 ticks,  T1L ~450 ns = 5 ticks (relaxed to 4)   */
static const rmt_bytes_encoder_config_t k_enc_cfg = {
    .bit0 = { .level0 = 1, .duration0 = 4, .level1 = 0, .duration1 = 8 },
    .bit1 = { .level0 = 1, .duration0 = 8, .level1 = 0, .duration1 = 4 },
    .flags = { .msb_first = 1 },
};

static const rmt_transmit_config_t k_tx_cfg = {
    .loop_count = 0,
    .flags = { .eot_level = 0 },  /* line stays LOW after last symbol → reset */
};

static rmt_channel_handle_t s_chan;
static rmt_encoder_handle_t s_enc;

static TimerHandle_t  s_timer;
static void         (*s_cb)(void *);
static void          *s_cb_arg;

static void timer_fired(TimerHandle_t t)
{
    (void)t;
    void (*cb)(void *) = s_cb;
    void  *arg         = s_cb_arg;
    s_cb     = NULL;
    s_cb_arg = NULL;
    if (cb) cb(arg);
}

/* WS2812B uses GRB byte order */
static void hal_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t grb[3] = {g, r, b};
    rmt_transmit(s_chan, s_enc, grb, sizeof(grb), &k_tx_cfg);
    rmt_tx_wait_all_done(s_chan, 10);
}

static void hal_schedule_ms(uint32_t ms, void (*cb)(void *), void *arg)
{
    s_cb     = cb;
    s_cb_arg = arg;
    xTimerChangePeriod(s_timer, pdMS_TO_TICKS(ms), 0);
    xTimerStart(s_timer, 0);
}

static void hal_cancel(void)
{
    xTimerStop(s_timer, 0);
    s_cb     = NULL;
    s_cb_arg = NULL;
}

imb_led_hal_t imb_led_rmt_init(void)
{
    rmt_tx_channel_config_t chan_cfg = {
        .gpio_num          = LED_GPIO,
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = LED_RMT_RES_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    rmt_new_tx_channel(&chan_cfg, &s_chan);
    rmt_new_bytes_encoder(&k_enc_cfg, &s_enc);
    rmt_enable(s_chan);

    s_timer = xTimerCreate("led", pdMS_TO_TICKS(100), pdFALSE, NULL, timer_fired);

    imb_led_hal_t hal = {
        .set_color   = hal_set_color,
        .schedule_ms = hal_schedule_ms,
        .cancel      = hal_cancel,
    };
    return hal;
}
