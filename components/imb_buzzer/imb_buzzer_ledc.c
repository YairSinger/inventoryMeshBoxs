#include "imb_buzzer_ledc.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#define BUZZER_GPIO       17
#define BUZZER_LEDC_MODE  LEDC_LOW_SPEED_MODE
#define BUZZER_LEDC_TIMER LEDC_TIMER_0
#define BUZZER_LEDC_CH    LEDC_CHANNEL_0
#define BUZZER_DUTY_RES   LEDC_TIMER_10_BIT
#define BUZZER_DUTY_50PCT 512   /* 50 % of 1024 */

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

static void hal_tone(uint32_t freq_hz)
{
    ledc_set_freq(BUZZER_LEDC_MODE, BUZZER_LEDC_TIMER, freq_hz);
    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CH, BUZZER_DUTY_50PCT);
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CH);
}

static void hal_silence(void)
{
    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CH, 0);
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CH);
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

imb_buzzer_hal_t imb_buzzer_ledc_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = BUZZER_LEDC_MODE,
        .duty_resolution = BUZZER_DUTY_RES,
        .timer_num       = BUZZER_LEDC_TIMER,
        .freq_hz         = 2700,   /* default; will be overridden per tone */
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_cfg);

    ledc_channel_config_t ch_cfg = {
        .gpio_num   = BUZZER_GPIO,
        .speed_mode = BUZZER_LEDC_MODE,
        .channel    = BUZZER_LEDC_CH,
        .timer_sel  = BUZZER_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config(&ch_cfg);

    s_timer = xTimerCreate("buzzer", pdMS_TO_TICKS(50), pdFALSE, NULL, timer_fired);

    imb_buzzer_hal_t hal = {
        .tone        = hal_tone,
        .silence     = hal_silence,
        .schedule_ms = hal_schedule_ms,
        .cancel      = hal_cancel,
    };
    return hal;
}
