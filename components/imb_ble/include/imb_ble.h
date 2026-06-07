#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "imb_protocol.h"
#include "imb_types.h"

/* ── Callback struct ───────────────────────────────────────────────────── */

typedef struct {
    /* Client connected — start HELLO timeout */
    void (*on_connected)(void *ctx);
    /* EVENT_NOTIFY CCCD enabled — safe to flush queued events */
    void (*on_subscribed)(void *ctx);
    /* Raw bytes from COMMAND_WRITE characteristic */
    void (*on_cmd)(void *ctx, const uint8_t *buf, size_t len);
    /* Connection dropped */
    void (*on_disconnected)(void *ctx);
} imb_ble_callbacks_t;

/* ── Connection parameter profiles ────────────────────────────────────── */

typedef enum {
    IMB_BLE_CONN_FIELD_CHECK,    /* 15–30 ms, latency=0, supervision=2 s  */
    IMB_BLE_CONN_REGISTRATION,   /* 100–200 ms, latency=4, supervision=6 s */
} imb_ble_conn_profile_e;

/* ── Public API ────────────────────────────────────────────────────────── */

/* Init NimBLE, register GATT table, start advertising. cbs/ctx must remain
   valid for the lifetime of the component. */
esp_err_t imb_ble_init(const imb_ble_callbacks_t *cbs, void *ctx);

/* Send raw pre-packed bytes on EVENT_NOTIFY characteristic. */
esp_err_t imb_ble_notify_event(const uint8_t *buf, size_t len);

/* Send raw pre-packed bytes on REPORT_NOTIFY characteristic. */
esp_err_t imb_ble_notify_report(const uint8_t *buf, size_t len);

/* Stop + restart advertising with new manufacturer data.
   box_name used to form "IMB-<name>-<last4MAC>"; if pin_hash==0 uses
   "IMB-SETUP-<last4MAC>". */
esp_err_t imb_ble_update_adv(uint32_t pin_hash, imb_op_mode_e mode,
                              uint8_t mesh_epoch, const char *box_name);

/* Re-request connection parameters (called after mode changes). */
esp_err_t imb_ble_set_conn_params(imb_ble_conn_profile_e profile);

/* Force-disconnect the current client. */
void imb_ble_disconnect(void);

/* Erase NimBLE bond for the currently connected peer. */
void imb_ble_unpair_current(void);
