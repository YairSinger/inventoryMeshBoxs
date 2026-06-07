/* LED Color Contract Demo — WS2812B on GPIO 48
 * Cycles through every pattern in docs/protocols.md, 5s each, with serial print.
 * RMT TX channel, 10 MHz clock (100ns/tick), GRB byte order. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/rmt_tx.h"
#include "esp_check.h"

#define LED_GPIO     48
#define RMT_RES_HZ   10000000   /* 10 MHz → 100 ns / tick */

/* WS2812B timing in 100 ns ticks */
#define WS_T0H   4   /* 400 ns */
#define WS_T0L   9   /* 900 ns */
#define WS_T1H   8   /* 800 ns */
#define WS_T1L   5   /* 500 ns */
#define WS_RST   500 /* 50 µs low reset */

/* ------------------------------------------------------------------ */
/* Encoder                                                             */
/* ------------------------------------------------------------------ */
typedef struct {
    rmt_encoder_t     base;
    rmt_encoder_t    *bytes_encoder;
    rmt_encoder_t    *copy_encoder;
    rmt_symbol_word_t reset_sym;
    int               state;
} ws2812_encoder_t;

static size_t ws2812_encode(rmt_encoder_t *encoder, rmt_channel_handle_t ch,
                             const void *data, size_t len, rmt_encode_state_t *out_state)
{
    ws2812_encoder_t *enc = __containerof(encoder, ws2812_encoder_t, base);
    rmt_encode_state_t sub = RMT_ENCODING_RESET;
    size_t n = 0;

    switch (enc->state) {
    case 0:
        n += enc->bytes_encoder->encode(enc->bytes_encoder, ch, data, len, &sub);
        if (sub & RMT_ENCODING_COMPLETE) enc->state = 1;
        if (sub & RMT_ENCODING_MEM_FULL) { *out_state |= RMT_ENCODING_MEM_FULL; return n; }
        /* fall through */
    case 1:
        n += enc->copy_encoder->encode(enc->copy_encoder, ch,
                                       &enc->reset_sym, sizeof(enc->reset_sym), &sub);
        if (sub & RMT_ENCODING_COMPLETE) {
            enc->state = 0;
            *out_state |= RMT_ENCODING_COMPLETE;
        }
        if (sub & RMT_ENCODING_MEM_FULL) *out_state |= RMT_ENCODING_MEM_FULL;
        break;
    }
    return n;
}

static esp_err_t ws2812_del(rmt_encoder_t *encoder)
{
    ws2812_encoder_t *enc = __containerof(encoder, ws2812_encoder_t, base);
    rmt_del_encoder(enc->bytes_encoder);
    rmt_del_encoder(enc->copy_encoder);
    free(enc);
    return ESP_OK;
}

static esp_err_t ws2812_reset_enc(rmt_encoder_t *encoder)
{
    ws2812_encoder_t *enc = __containerof(encoder, ws2812_encoder_t, base);
    rmt_encoder_reset(enc->bytes_encoder);
    rmt_encoder_reset(enc->copy_encoder);
    enc->state = 0;
    return ESP_OK;
}

static esp_err_t ws2812_new_encoder(rmt_encoder_handle_t *ret)
{
    ws2812_encoder_t *enc = calloc(1, sizeof(*enc));
    if (!enc) return ESP_ERR_NO_MEM;

    enc->base.encode = ws2812_encode;
    enc->base.del    = ws2812_del;
    enc->base.reset  = ws2812_reset_enc;

    rmt_bytes_encoder_config_t bcfg = {
        .bit0 = { .level0 = 1, .duration0 = WS_T0H,
                  .level1 = 0, .duration1 = WS_T0L },
        .bit1 = { .level0 = 1, .duration0 = WS_T1H,
                  .level1 = 0, .duration1 = WS_T1L },
        .flags.msb_first = 1,
    };
    ESP_RETURN_ON_ERROR(rmt_new_bytes_encoder(&bcfg, &enc->bytes_encoder),
                        "LED", "bytes encoder");

    rmt_copy_encoder_config_t ccfg = {};
    ESP_RETURN_ON_ERROR(rmt_new_copy_encoder(&ccfg, &enc->copy_encoder),
                        "LED", "copy encoder");

    enc->reset_sym.level0    = 0;
    enc->reset_sym.duration0 = WS_RST / 2;
    enc->reset_sym.level1    = 0;
    enc->reset_sym.duration1 = WS_RST / 2;

    *ret = &enc->base;
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Low-level helpers                                                   */
/* ------------------------------------------------------------------ */
static rmt_channel_handle_t s_ch;
static rmt_encoder_handle_t s_enc;
static const rmt_transmit_config_t s_tx_cfg = { .loop_count = 0 };

typedef struct { uint8_t g, r, b; } grb_t;

static void led_set(grb_t px)
{
    rmt_transmit(s_ch, s_enc, &px, sizeof(px), &s_tx_cfg);
    rmt_tx_wait_all_done(s_ch, portMAX_DELAY);
}

static void led_off(void) { led_set((grb_t){0, 0, 0}); }
static void delay_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

static uint8_t scale(uint8_t c, uint8_t bri)
{
    return (uint8_t)(((uint16_t)c * bri) >> 8);
}

static void breathe(grb_t colour, uint32_t period_ms, uint32_t total_ms)
{
    uint32_t step_ms = 20, elapsed = 0;
    while (elapsed < total_ms) {
        uint32_t phase = (elapsed % period_ms) * 512 / period_ms;
        uint8_t bri = (phase < 256) ? (uint8_t)phase : (uint8_t)(511 - phase);
        led_set((grb_t){scale(colour.g, bri), scale(colour.r, bri), scale(colour.b, bri)});
        delay_ms(step_ms);
        elapsed += step_ms;
    }
}

/* ------------------------------------------------------------------ */
/* Pattern library (docs/protocols.md LED Color Contract)             */
/* ------------------------------------------------------------------ */
static void pat_insert(void)
{
    puts("[LED] Item inserted — GREEN single pulse");
    led_set((grb_t){200, 0, 0}); delay_ms(300); led_off(); delay_ms(4700);
}
static void pat_extract(void)
{
    puts("[LED] Item extracted — RED single pulse");
    led_set((grb_t){0, 200, 0}); delay_ms(300); led_off(); delay_ms(4700);
}
static void pat_ambiguous(void)
{
    puts("[LED] AMBIGUOUS scan — YELLOW single flash");
    led_set((grb_t){180, 180, 0}); delay_ms(200); led_off(); delay_ms(4800);
}
static void pat_reg_pass(void)
{
    puts("[LED] Registration pass (NDEF written) — SOLID GREEN 2 s");
    led_set((grb_t){200, 0, 0}); delay_ms(2000); led_off(); delay_ms(3000);
}
static void pat_reg_fail(void)
{
    puts("[LED] Registration fail (NDEF write error) — RED double-flash");
    grb_t red = {0, 200, 0};
    led_set(red); delay_ms(200); led_off(); delay_ms(200);
    led_set(red); delay_ms(200); led_off(); delay_ms(4400);
}
static void pat_mesh_disco(void)
{
    puts("[LED] Mesh disconnected — BLUE slow breathing");
    breathe((grb_t){0, 0, 200}, 2000, 5000); led_off();
}
static void pat_ble_idle(void)
{
    puts("[LED] BLE connected idle — WHITE dim pulse every 3 s");
    grb_t white = {60, 60, 60};
    for (int i = 0; i < 2; i++) {
        led_set(white); delay_ms(300); led_off(); delay_ms(2700);
    }
}
static void pat_factory_hold(void)
{
    puts("[LED] Factory reset hold (BOOT 10 s) — SLOW RED breathing");
    breathe((grb_t){0, 200, 0}, 3000, 5000); led_off();
}
static void pat_factory_fire(void)
{
    puts("[LED] Factory reset triggered — FAST RED flash (no reboot in demo)");
    grb_t red = {0, 200, 0};
    for (int i = 0; i < 10; i++) { led_set(red); delay_ms(250); led_off(); delay_ms(250); }
}
static void pat_sleep(void)
{
    puts("[LED] Deep sleep — OFF");
    led_off(); delay_ms(5000);
}

/* ------------------------------------------------------------------ */
/* app_main                                                            */
/* ------------------------------------------------------------------ */
void app_main(void)
{
    printf("\n=== IMB LED Color Contract Demo (GPIO %d) ===\n", LED_GPIO);
    printf("Each pattern held for 5 s. See docs/protocols.md.\n\n");

    rmt_tx_channel_config_t ch_cfg = {
        .gpio_num          = LED_GPIO,
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = RMT_RES_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&ch_cfg, &s_ch));
    ESP_ERROR_CHECK(ws2812_new_encoder(&s_enc));
    ESP_ERROR_CHECK(rmt_enable(s_ch));

    while (1) {
        pat_insert(); pat_extract(); pat_ambiguous();
        pat_reg_pass(); pat_reg_fail(); pat_mesh_disco();
        pat_ble_idle(); pat_factory_hold(); pat_factory_fire(); pat_sleep();
        printf("\n--- Cycle complete, restarting ---\n\n");
    }
}
