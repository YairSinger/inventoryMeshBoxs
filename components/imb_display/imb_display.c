#include "imb_display.h"
#include <string.h>
#include <stdio.h>

/* ── Internal state machine ──────────────────────────────────────────────── */

typedef enum {
    SCREEN_IDLE,
    SCREEN_EVENT,
    SCREEN_REPORT,
    SCREEN_ERROR,
} screen_state_e;

static imb_display_hal_t    g_hal;
static imb_display_ctx_t    g_ctx;
static screen_state_e       g_screen;
static imb_display_report_t g_report;
static uint8_t              g_report_idx;  /* current item index during cycling */

/* ── Render helpers ──────────────────────────────────────────────────────── */

static void render_idle(void)
{
    char buf[64];
    g_hal.clear();

    switch (g_ctx.op_mode) {
    case IMB_MODE_SETUP:
        g_hal.draw_text(0, 0, "SETUP");
        g_hal.draw_text(1, 0, g_ctx.last4_mac);
        break;

    case IMB_MODE_REGISTRATION:
        g_hal.draw_text(0, 0, g_ctx.box_name);
        g_hal.draw_text(1, 0, "REGISTRATION");
        snprintf(buf, sizeof(buf), "pending: %u", g_ctx.pending_tag_count);
        g_hal.draw_text(2, 0, buf);
        break;

    case IMB_MODE_REGISTRATION_INCOMPLETE:
        g_hal.draw_text(0, 0, g_ctx.box_name);
        g_hal.draw_text(1, 0, "INCOMPLETE");
        break;

    case IMB_MODE_FIELD_CHECK:
    default:
        g_hal.draw_text(0, 0, g_ctx.box_name);
        g_hal.draw_text(1, 0, "FIELD CHECK");
        if (g_ctx.mesh_peer_count == IMB_DISPLAY_MESH_UNKNOWN) {
            snprintf(buf, sizeof(buf), "mesh: ?");
        } else {
            snprintf(buf, sizeof(buf), "mesh: %u", g_ctx.mesh_peer_count);
        }
        g_hal.draw_text(2, 0, buf);
        g_hal.draw_text(3, 0, g_ctx.phone_connected ? "PHONE connected" : "no phone");
        break;
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void imb_display_init(const imb_display_hal_t *hal)
{
    g_hal    = *hal;
    g_ctx    = (imb_display_ctx_t){0};
    g_screen = SCREEN_IDLE;
}

void imb_display_update_ctx(const imb_display_ctx_t *ctx)
{
    g_ctx = *ctx;
    if (g_screen == SCREEN_IDLE) {
        render_idle();
    }
}

static void on_event_timer(void *arg);
static void on_report_timer(void *arg);

static void render_error(const char *label, const char *detail)
{
    g_hal.clear();
    g_hal.draw_text(0, 0, label);
    if (detail && detail[0]) {
        g_hal.draw_text(1, 0, detail);
    }
    g_screen = SCREEN_ERROR;
}

static void render_report_item(void)
{
    char buf[64];
    uint8_t total_items = g_report.missing_count
                        + g_report.foreign_count
                        + g_report.ambiguous_count;

    if (g_report_idx < g_report.missing_count) {
        uint8_t i = g_report_idx;
        snprintf(buf, sizeof(buf), "MISSING %u/%u", (unsigned)(i + 1),
                 (unsigned)total_items);
        g_hal.clear();
        g_hal.draw_text(0, 0, buf);
        g_hal.draw_text(1, 0, g_report.missing[i]);
    } else if (g_report_idx < g_report.missing_count + g_report.foreign_count) {
        uint8_t i = g_report_idx - g_report.missing_count;
        snprintf(buf, sizeof(buf), "FOREIGN %u/%u", (unsigned)(g_report_idx + 1),
                 (unsigned)total_items);
        g_hal.clear();
        g_hal.draw_text(0, 0, buf);
        g_hal.draw_text(1, 0, g_report.foreign[i]);
    } else {
        uint8_t i = g_report_idx - g_report.missing_count - g_report.foreign_count;
        snprintf(buf, sizeof(buf), "AMBIGUOUS %u/%u", (unsigned)(g_report_idx + 1),
                 (unsigned)total_items);
        g_hal.clear();
        g_hal.draw_text(0, 0, buf);
        g_hal.draw_text(1, 0, g_report.ambiguous[i][0] ? g_report.ambiguous[i] : "?");
    }

    g_hal.schedule_ms(2000, on_report_timer, NULL);
}

static void on_report_timer(void *arg)
{
    (void)arg;
    uint8_t total = g_report.missing_count + g_report.foreign_count + g_report.ambiguous_count;
    g_report_idx++;
    if (g_report_idx < total) {
        render_report_item();
    } else {
        g_screen = SCREEN_IDLE;
        render_idle();
    }
}

static void on_event_timer(void *arg)
{
    (void)arg;
    g_screen = SCREEN_IDLE;
    render_idle();
}

void imb_display_on_detection(imb_direction_e dir, const char *item_name)
{
    g_hal.cancel();

    if (!item_name || item_name[0] == '\0') {
        render_error("UNKNOWN TAG", NULL);
        return;
    }

    g_hal.clear();
    g_hal.draw_text(0, 0, dir == IMB_INSERT ? "INSERT" : "EXTRACT");
    g_hal.draw_text(1, 0, item_name);
    g_screen = SCREEN_EVENT;
    g_hal.schedule_ms(5000, on_event_timer, NULL);
}

void imb_display_on_ambiguous(const char *item_name)
{
    g_hal.cancel();
    render_error("AMBIGUOUS", item_name ? item_name : "?");
}

void imb_display_on_report(const imb_display_report_t *report)
{
    g_hal.cancel();
    g_report     = *report;
    g_report_idx = 0;

    uint8_t total = report->missing_count + report->foreign_count + report->ambiguous_count;
    if (total == 0) {
        g_screen = SCREEN_IDLE;
        render_idle();
        return;
    }

    g_screen = SCREEN_REPORT;
    render_report_item();
}
