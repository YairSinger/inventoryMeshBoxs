#include "imb_ble.h"
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "IMB_BLE";

/* ── State ──────────────────────────────────────────────────────────────── */

static imb_ble_callbacks_t g_cbs;
static void               *g_ctx;

static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t g_char_event_val_handle;
static uint16_t g_char_report_val_handle;
static uint16_t g_char_cmd_val_handle;

/* Adv state — updated via imb_ble_update_adv() */
static struct {
    uint32_t     pin_hash;
    imb_op_mode_e mode;
    uint8_t      mesh_epoch;
    char         box_name[IMB_NAME_LEN];
} g_adv;

/* Last 4 hex chars of MAC, set on sync */
static char g_mac4[5];  /* "AABB\0" */

/* Subscribe tracking — fire on_subscribed only after both CCCDs enabled */
static bool g_event_sub;
static bool g_report_sub;

/* ── Prototypes ─────────────────────────────────────────────────────────── */

static int  gap_event_cb(struct ble_gap_event *event, void *arg);
static void start_advertising(void);

/* ── GATT table ─────────────────────────────────────────────────────────── */
/*
 * UUID byte arrays are the 128-bit UUIDs in little-endian order:
 *   e5d50000-01d0-47e0-afc5-01e466d9298e → 8e 29 d9 66 e4 01 c5 af e0 47 d0 01 00 00 d5 e5
 *   last 16-bit field: 0001 / 0002 / 0003 for the three characteristics
 */
/* Notify-only characteristics still need a non-NULL access_cb (NimBLE rejects
   NULL with BLE_HS_EINVAL). This stub is never invoked for notify-only flags. */
static int notify_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)ctxt; (void)arg;
    return BLE_ATT_ERR_UNLIKELY;
}

static int cmd_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;

    uint8_t  buf[IMB_PROTO_BUF_MAX];
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len > sizeof(buf)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), &len);

    if (g_cbs.on_cmd) g_cbs.on_cmd(g_ctx, buf, len);
    return 0;
}

static const struct ble_gatt_svc_def g_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID128_DECLARE(
            0x8e, 0x29, 0xd9, 0x66, 0xe4, 0x01, 0xc5, 0xaf,
            0xe0, 0x47, 0xd0, 0x01, 0x00, 0x00, 0xd5, 0xe5),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {   /* EVENT_NOTIFY — 0x0001 */
                .uuid = BLE_UUID128_DECLARE(
                    0x8e, 0x29, 0xd9, 0x66, 0xe4, 0x01, 0xc5, 0xaf,
                    0xe0, 0x47, 0xd0, 0x01, 0x01, 0x00, 0xd5, 0xe5),
                .access_cb  = notify_access_cb,
                .flags      = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &g_char_event_val_handle,
            },
            {   /* REPORT_NOTIFY — 0x0002 */
                .uuid = BLE_UUID128_DECLARE(
                    0x8e, 0x29, 0xd9, 0x66, 0xe4, 0x01, 0xc5, 0xaf,
                    0xe0, 0x47, 0xd0, 0x01, 0x02, 0x00, 0xd5, 0xe5),
                .access_cb  = notify_access_cb,
                .flags      = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &g_char_report_val_handle,
            },
            {   /* COMMAND_WRITE — 0x0003 */
                .uuid = BLE_UUID128_DECLARE(
                    0x8e, 0x29, 0xd9, 0x66, 0xe4, 0x01, 0xc5, 0xaf,
                    0xe0, 0x47, 0xd0, 0x01, 0x03, 0x00, 0xd5, 0xe5),
                .access_cb  = cmd_write_cb,
                .flags      = BLE_GATT_CHR_F_WRITE,
                .val_handle = &g_char_cmd_val_handle,
            },
            { 0 }
        },
    },
    { 0 }
};

/* ── Advertising ────────────────────────────────────────────────────────── */

static void start_advertising(void)
{
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    /* Device name: "IMB-SETUP-<last4MAC>" in SETUP / zero-PIN, else "IMB-<name>-<last4MAC>" */
    char name[64];
    if (g_adv.pin_hash == 0) {
        snprintf(name, sizeof(name), "IMB-SETUP-%s", g_mac4);
    } else {
        snprintf(name, sizeof(name), "IMB-%s-%s", g_adv.box_name, g_mac4);
    }
    fields.name             = (uint8_t *)name;
    fields.name_len         = (uint8_t)strlen(name);
    fields.name_is_complete = 1;

    /* 8-byte manufacturer data */
    imb_pkt_adv_t mfg = {
        .company_id = 0xFFFF,
        .pin_hash   = g_adv.pin_hash,
        .op_mode    = (uint8_t)g_adv.mode,
        .mesh_epoch = g_adv.mesh_epoch,
    };
    fields.mfg_data     = (uint8_t *)&mfg;
    fields.mfg_data_len = sizeof(mfg);

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields rc=%d", rc);
        return;
    }

    struct ble_gap_adv_params params;
    memset(&params, 0, sizeof(params));
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.itvl_min  = 160;  /* 100 ms / 0.625 ms per unit */
    params.itvl_max  = 200;  /* 125 ms */

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &params, gap_event_cb, NULL);
    if (rc != 0) ESP_LOGE(TAG, "adv_start rc=%d", rc);
}

/* ── GAP event handler ──────────────────────────────────────────────────── */

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    struct ble_gap_conn_desc desc;
    switch (event->type) {

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            ESP_LOGE(TAG, "connect failed status=%d", event->connect.status);
            start_advertising();
            return 0;
        }
        /* Single-client enforcement */
        if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            ESP_LOGW(TAG, "second client rejected");
            ble_gap_terminate(event->connect.conn_handle, BLE_ERR_CONN_REJ_RESOURCES);
            return 0;
        }
        g_conn_handle  = event->connect.conn_handle;
        g_event_sub    = false;
        g_report_sub   = false;
        if (ble_gap_conn_find(event->connect.conn_handle, &desc) == 0) {
            ESP_LOGI(TAG, "connected handle=%d peer=%02X:%02X:%02X:%02X:%02X:%02X type=%d bonded=%d",
                     g_conn_handle,
                     desc.peer_id_addr.val[5], desc.peer_id_addr.val[4],
                     desc.peer_id_addr.val[3], desc.peer_id_addr.val[2],
                     desc.peer_id_addr.val[1], desc.peer_id_addr.val[0],
                     desc.peer_id_addr.type,
                     desc.sec_state.bonded);
        } else {
            ESP_LOGI(TAG, "connected handle=%d", g_conn_handle);
        }
        if (g_cbs.on_connected) g_cbs.on_connected(g_ctx);
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected reason=%d (0x%02X)", event->disconnect.reason,
                 event->disconnect.reason);
        g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        g_event_sub   = false;
        g_report_sub  = false;
        if (g_cbs.on_disconnected) g_cbs.on_disconnected(g_ctx);
        start_advertising();
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == g_char_event_val_handle)
            g_event_sub  = (event->subscribe.cur_notify == 1);
        else if (event->subscribe.attr_handle == g_char_report_val_handle)
            g_report_sub = (event->subscribe.cur_notify == 1);
        ESP_LOGI(TAG, "subscribe attr=%d notify=%d event_sub=%d report_sub=%d",
                 event->subscribe.attr_handle, event->subscribe.cur_notify,
                 g_event_sub, g_report_sub);
        if (g_event_sub && g_report_sub) {
            ESP_LOGI(TAG, "both CCCDs subscribed");
            if (g_cbs.on_subscribed) g_cbs.on_subscribed(g_ctx);
        }
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU negotiated=%d", event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "enc_change handle=%d status=%d encrypted=%d authenticated=%d bonded=%d",
                 event->enc_change.conn_handle,
                 event->enc_change.status,
                 (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0) ? desc.sec_state.encrypted : -1,
                 (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0) ? desc.sec_state.authenticated : -1,
                 (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0) ? desc.sec_state.bonded : -1);
        if (event->enc_change.status != 0)
            ESP_LOGE(TAG, "ENC FAILED — pairing/bonding error status=0x%02X", event->enc_change.status);
        return 0;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        /* Just Works — should not normally fire, log it if it does */
        ESP_LOGW(TAG, "passkey_action action=%d (unexpected for Just Works)",
                 event->passkey.params.action);
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* Phone is trying to pair but a bond already exists for this peer.
         * Delete the stale bond and allow the new pairing to proceed. */
        ESP_LOGW(TAG, "repeat_pairing — stale bond found, deleting and retrying");
        ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        ble_store_util_delete_peer(&desc.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    case BLE_GAP_EVENT_IDENTITY_RESOLVED:
        if (ble_gap_conn_find(event->identity_resolved.conn_handle, &desc) == 0) {
            ESP_LOGI(TAG, "identity resolved peer=%02X:%02X:%02X:%02X:%02X:%02X bonded=%d",
                     desc.peer_id_addr.val[5], desc.peer_id_addr.val[4],
                     desc.peer_id_addr.val[3], desc.peer_id_addr.val[2],
                     desc.peer_id_addr.val[1], desc.peer_id_addr.val[0],
                     desc.sec_state.bonded);
        }
        return 0;

    default:
        return 0;
    }
}

/* ── NimBLE host task ───────────────────────────────────────────────────── */

static void host_task(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void on_sync(void)
{
    /* Capture last 2 bytes of public addr as 4 hex chars */
    uint8_t addr[6];
    int     addrtype;
    ble_hs_id_copy_addr(BLE_ADDR_PUBLIC, addr, &addrtype);
    snprintf(g_mac4, sizeof(g_mac4), "%02X%02X", addr[1], addr[0]);

    start_advertising();
}

/* ── Public API ─────────────────────────────────────────────────────────── */

esp_err_t imb_ble_init(const imb_ble_callbacks_t *cbs, void *ctx)
{
    g_cbs = *cbs;
    g_ctx = ctx;

    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) { ESP_LOGE(TAG, "nimble_port_init failed: %d", ret); return ret; }

    /* Security: Just Works + LE Secure Connections + bonding */
    ble_hs_cfg.sm_sc           = 1;
    ble_hs_cfg.sm_bonding      = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sync_cb         = on_sync;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(g_svcs);
    if (rc != 0) { ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc); return ESP_FAIL; }
    rc = ble_gatts_add_svcs(g_svcs);
    if (rc != 0) { ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc); return ESP_FAIL; }

    xTaskCreate(host_task, "ble_host", 4096, NULL, 5, NULL);
    return ESP_OK;
}

static esp_err_t notify(uint16_t val_handle, const uint8_t *buf, size_t len)
{
    if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE) return ESP_ERR_INVALID_STATE;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, len);
    if (!om) return ESP_ERR_NO_MEM;
    int rc = ble_gattc_notify_custom(g_conn_handle, val_handle, om);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t imb_ble_notify_event(const uint8_t *buf, size_t len)
{
    return notify(g_char_event_val_handle, buf, len);
}

esp_err_t imb_ble_notify_report(const uint8_t *buf, size_t len)
{
    return notify(g_char_report_val_handle, buf, len);
}

esp_err_t imb_ble_update_adv(uint32_t pin_hash, imb_op_mode_e mode,
                              uint8_t mesh_epoch, const char *box_name)
{
    g_adv.pin_hash    = pin_hash;
    g_adv.mode        = mode;
    g_adv.mesh_epoch  = mesh_epoch;
    strncpy(g_adv.box_name, box_name, IMB_NAME_LEN - 1);
    g_adv.box_name[IMB_NAME_LEN - 1] = '\0';

    ble_gap_adv_stop();
    start_advertising();
    return ESP_OK;
}

esp_err_t imb_ble_set_conn_params(imb_ble_conn_profile_e profile)
{
    if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE) return ESP_ERR_INVALID_STATE;

    struct ble_gap_upd_params params;
    memset(&params, 0, sizeof(params));

    if (profile == IMB_BLE_CONN_FIELD_CHECK) {
        params.itvl_min    = BLE_GAP_CONN_ITVL_MS(15);
        params.itvl_max    = BLE_GAP_CONN_ITVL_MS(30);
        params.latency     = 0;
        params.supervision_timeout = BLE_GAP_SUPERVISION_TIMEOUT_MS(2000);
    } else {
        params.itvl_min    = BLE_GAP_CONN_ITVL_MS(100);
        params.itvl_max    = BLE_GAP_CONN_ITVL_MS(200);
        params.latency     = 4;
        params.supervision_timeout = BLE_GAP_SUPERVISION_TIMEOUT_MS(6000);
    }

    int rc = ble_gap_update_params(g_conn_handle, &params);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

void imb_ble_disconnect(void)
{
    if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(g_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

void imb_ble_unpair_current(void)
{
    if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(g_conn_handle, &desc) == 0)
        ble_gap_unpair(&desc.peer_id_addr);
}

void imb_ble_clear_all_bonds(void)
{
    ble_store_clear();
}
