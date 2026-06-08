#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "imb_protocol.h"
#include "imb_types.h"

/* ── Timer HAL ──────────────────────────────────────────────────────────── */

typedef void (*imb_session_timer_cb_t)(void *arg);

typedef struct {
    void (*start)(uint32_t ms, imb_session_timer_cb_t cb, void *arg, void *ctx);
    void (*stop)(void *ctx);
    void *ctx;
} imb_ble_session_timer_hal_t;

/* ── NVS HAL ────────────────────────────────────────────────────────────── */

typedef struct {
    int  (*read_op_mode)(imb_op_mode_e *out, void *ctx);
    int  (*write_op_mode)(imb_op_mode_e mode, void *ctx);
    /* uids: array of IMB_UID_LEN-byte strings; count_out set to number read */
    int  (*read_pending_uids)(char uids[][IMB_UID_LEN], uint8_t *count_out, void *ctx);
    int  (*write_pending_uids)(const char uids[][IMB_UID_LEN], uint8_t count, void *ctx);
    void *ctx;
} imb_ble_session_nvs_hal_t;

/* ── BLE HAL ────────────────────────────────────────────────────────────── */

typedef struct {
    int  (*notify_event)(const uint8_t *buf, size_t len, void *ctx);
    int  (*notify_report)(const uint8_t *buf, size_t len, void *ctx);
    void (*disconnect)(void *ctx);
    void (*unbond)(void *ctx);   /* erase NimBLE bond for current peer */
    void *ctx;
} imb_ble_session_ble_hal_t;

/* ── App callbacks ──────────────────────────────────────────────────────── */

typedef struct {
    void (*on_name_tag)(void *ctx, const char *uid, const char *name, uint8_t msg_id);
    void (*on_accept_tag)(void *ctx, const char *uid, uint8_t accepted, uint8_t msg_id);
    void (*on_mode_set)(void *ctx, imb_op_mode_e mode, uint8_t msg_id);
    /* pin_hash + box_name: app must persist to NVS and call imb_ble_update_adv() */
    void (*on_set_pin)(void *ctx, uint32_t pin_hash, const char *box_name, uint8_t msg_id);
    /* new_name: app must persist to NVS and call imb_ble_update_adv() */
    void (*on_box_rename)(void *ctx, const char *new_name, uint8_t msg_id);
    void (*on_report_delivered)(void *ctx, bool success);
    void *ctx;
} imb_ble_session_app_cbs_t;

/* ── Config ─────────────────────────────────────────────────────────────── */

typedef struct {
    uint32_t pin_hash;

    const imb_ble_session_nvs_hal_t *nvs;
    const imb_ble_session_ble_hal_t *ble;
    const imb_ble_session_app_cbs_t *app;

    imb_ble_session_timer_hal_t hello_timer;  /* 5 s HELLO gate */
    imb_ble_session_timer_hal_t grace_timer;  /* 60 s REGISTRATION grace window */
} imb_ble_session_config_t;

/* Entries per REPORT_NOTIFY chunk (MTU=247, ATT=3, hdr=9 → 235 bytes / 50 bytes per entry) */
#define IMB_SESSION_ENTRIES_PER_CHUNK 4

/* ── Public API ─────────────────────────────────────────────────────────── */

void imb_ble_session_init(const imb_ble_session_config_t *cfg);

/* Wired to a new on_connected callback (starts HELLO timeout) */
void imb_ble_session_on_connected(void *ctx);

/* Wired to imb_ble_callbacks_t.on_subscribed */
void imb_ble_session_on_subscribed(void *ctx);

/* Wired to imb_ble_callbacks_t.on_cmd */
void imb_ble_session_on_cmd(void *ctx, const uint8_t *buf, size_t len);

/* Wired to imb_ble_callbacks_t.on_disconnected */
void imb_ble_session_on_disconnected(void *ctx);

/* Called by app after async hardware op completes (CMD_NAME, CMD_ACCEPT, etc.) */
void imb_ble_session_ack(uint8_t msg_id, imb_ack_status_e status);

/* Push a tag event to the 8-slot queue (called from NFC scan loop) */
int imb_ble_session_push_event_tag(const imb_pkt_event_tag_t *event);

/* Begin fragmented report delivery on lid close */
int imb_ble_session_deliver_report(const imb_pkt_report_entry_t *entries, uint16_t count);
