#include "imb_ble.h"
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include <string.h>

static const char *TAG = "IMB_BLE";

/* ── NimBLE state ───────────────────────────────────────────────────────── */

static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static imb_ble_config_t g_config;
static imb_op_mode_e g_current_mode;
static bool g_is_authed = false;
static TimerHandle_t g_hello_timer = NULL;

static uint16_t g_char_event_val_handle;
static uint16_t g_char_report_val_handle;
static uint16_t g_char_command_val_handle;

/* ── Prototypes ─────────────────────────────────────────────────────────── */

static int imb_ble_gap_event(struct ble_gap_event *event, void *arg);
static void imb_ble_advertise(void);
static void imb_ble_hello_timeout_cb(TimerHandle_t xTimer);

/* ── GATT Server ────────────────────────────────────────────────────────── */

static int imb_ble_chr_write_command(uint16_t conn_handle, uint16_t attr_handle,
                                    struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;

    uint8_t buf[IMB_PROTO_BUF_MAX];
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len > sizeof(buf)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;

    ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), &len);

    imb_cmd_header_t hdr;
    if (len < sizeof(hdr)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    memcpy(&hdr, buf, sizeof(hdr));

    /* CMD_HELLO is the only command allowed before authentication */
    if (!g_is_authed && hdr.msg_type != IMB_MSG_CMD_HELLO) {
        ESP_LOGW(TAG, "Rejecting command 0x%02X: NOT_AUTHED", hdr.msg_type);
        imb_ble_send_ack(hdr.msg_id, hdr.msg_type, IMB_ACK_NOT_AUTHED);
        return 0;
    }

    /* Stop HELLO timer if we got HELLO */
    if (hdr.msg_type == IMB_MSG_CMD_HELLO) {
        xTimerStop(g_hello_timer, 0);
        
        imb_pkt_cmd_hello_t hello;
        if (imb_proto_unpack_cmd_hello(buf, len, &hello) == 0) {
            if (hello.pin_hash == g_config.pin_hash) {
                ESP_LOGI(TAG, "Authentication SUCCESS");
                g_is_authed = true;
                imb_ble_send_ack(hello.msg_id, hello.msg_type, IMB_ACK_OK);
            } else {
                ESP_LOGW(TAG, "Authentication FAILED (PIN mismatch)");
                imb_ble_send_ack(hello.msg_id, hello.msg_type, IMB_ACK_PIN_MISMATCH);
                /* Disconnect will be handled by client or we can force it after short delay */
            }
        }
        return 0;
    }

    /* Dispatch other commands to logic layer */
    if (g_config.on_cmd) {
        g_config.on_cmd(&hdr, buf + sizeof(hdr));
    }

    return 0;
}

static const struct ble_gatt_svc_def g_imb_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID128_DECLARE(0x8e, 0x29, 0xd9, 0x66, 0xe4, 0x01, 0xc5, 0xaf, 0xe0, 0x47, 0xd0, 0x01, 0x00, 0x00, 0xd5, 0xe5),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID128_DECLARE(0x8e, 0x29, 0xd9, 0x66, 0xe4, 0x01, 0xc5, 0xaf, 0xe0, 0x47, 0xd0, 0x01, 0x01, 0x00, 0xd5, 0xe5),
                .access_cb = NULL,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &g_char_event_val_handle,
            },
            {
                .uuid = BLE_UUID128_DECLARE(0x8e, 0x29, 0xd9, 0x66, 0xe4, 0x01, 0xc5, 0xaf, 0xe0, 0x47, 0xd0, 0x01, 0x02, 0x00, 0xd5, 0xe5),
                .access_cb = NULL,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &g_char_report_val_handle,
            },
            {
                .uuid = BLE_UUID128_DECLARE(0x8e, 0x29, 0xd9, 0x66, 0xe4, 0x01, 0xc5, 0xaf, 0xe0, 0x47, 0xd0, 0x01, 0x03, 0x00, 0xd5, 0xe5),
                .access_cb = imb_ble_chr_write_command,
                .flags = BLE_GATT_CHR_F_WRITE,
                .val_handle = &g_char_command_val_handle,
            },
            { 0 }
        },
    },
    { 0 }
};

/* ── GAP & Advertising ──────────────────────────────────────────────────── */

static void imb_ble_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    char name[64];
    snprintf(name, sizeof(name), "IMB-%s", g_config.box_name);
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    /* Manufacturer Specific Data (8 bytes) */
    imb_pkt_adv_t mfg_data = {
        .company_id = 0xFFFF,
        .pin_hash = g_config.pin_hash,
        .op_mode = (uint8_t)g_current_mode,
        .flags = 0, /* TODO: populate from logic state */
    };
    fields.mfg_data = (uint8_t *)&mfg_data;
    fields.mfg_data_len = sizeof(mfg_data);

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "error setting advertisement data; rc=%d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, imb_ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "error enabling advertisement; rc=%d", rc);
    }
}

static int imb_ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
                    ESP_LOGW(TAG, "Rejecting second concurrent connection");
                    ble_gap_terminate(event->connect.conn_handle, BLE_ERR_CONN_REJ_RESOURCES);
                    return 0;
                }
                ESP_LOGI(TAG, "Connected");
                g_conn_handle = event->connect.conn_handle;
                g_is_authed = false;
                xTimerStart(g_hello_timer, 0);
            } else {
                ESP_LOGE(TAG, "Connection failed; status=%d", event->connect.status);
                imb_ble_advertise();
            }
            return 0;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Disconnected; reason=%d", event->disconnect.reason);
            g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            g_is_authed = false;
            xTimerStop(g_hello_timer, 0);
            imb_ble_advertise();
            return 0;

        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "MTU updated to %d", event->mtu.value);
            return 0;
    }
    return 0;
}

static void imb_ble_hello_timeout_cb(TimerHandle_t xTimer)
{
    if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE && !g_is_authed) {
        ESP_LOGW(TAG, "HELLO timeout; disconnecting");
        ble_gap_terminate(g_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

/* ── Host Task ──────────────────────────────────────────────────────────── */

static void imb_ble_host_task(void *param)
{
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void imb_ble_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);
    imb_ble_advertise();
}

/* ── Public API ─────────────────────────────────────────────────────────── */

esp_err_t imb_ble_init(const imb_ble_config_t *config)
{
    g_config = *config;
    g_current_mode = config->initial_mode;

    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) return ret;

    ble_hs_cfg.sync_cb = imb_ble_on_sync;
    ble_hs_cfg.gatts_register_cb = NULL;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    
    ret = ble_gatts_count_cfg(g_imb_svcs);
    if (ret != 0) return ESP_FAIL;

    ret = ble_gatts_add_svcs(g_imb_svcs);
    if (ret != 0) return ESP_FAIL;

    ble_svc_gap_device_name_set(g_config.box_name);

    g_hello_timer = xTimerCreate("hello_tmr", pdMS_TO_TICKS(5000), pdFALSE, NULL, imb_ble_hello_timeout_cb);

    xTaskCreate(imb_ble_host_task, "ble_host", 4096, NULL, 5, NULL);

    return ESP_OK;
}

esp_err_t imb_ble_set_mode(imb_op_mode_e mode)
{
    g_current_mode = mode;
    /* Re-advertise with new mode in mfg_data */
    if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_adv_stop();
        imb_ble_advertise();
    }
    
    /* Notify connected client */
    if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE && g_is_authed) {
        imb_pkt_event_mode_t pkt = {
            .msg_type = IMB_MSG_EVENT_MODE,
            .mode = (uint8_t)mode
        };
        uint8_t buf[sizeof(pkt)];
        imb_proto_pack_event_mode(&pkt, buf, sizeof(buf));
        struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, sizeof(buf));
        ble_gattc_notify_custom(g_conn_handle, g_char_event_val_handle, om);
    }
    return ESP_OK;
}

esp_err_t imb_ble_notify_event(const imb_pkt_event_tag_t *event)
{
    if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE || !g_is_authed) return ESP_ERR_INVALID_STATE;

    uint8_t buf[sizeof(imb_pkt_event_tag_t)];
    size_t len = imb_proto_pack_event_tag(event, buf, sizeof(buf));
    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, len);
    return ble_gattc_notify_custom(g_conn_handle, g_char_event_val_handle, om);
}

esp_err_t imb_ble_send_ack(uint8_t acked_msg_id, uint8_t acked_msg_type, imb_ack_status_e status)
{
    if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE) return ESP_ERR_INVALID_STATE;

    imb_pkt_event_ack_t ack = {
        .msg_type = IMB_MSG_EVENT_ACK,
        .acked_msg_id = acked_msg_id,
        .acked_msg_type = acked_msg_type,
        .status = (uint8_t)status
    };
    uint8_t buf[sizeof(ack)];
    imb_proto_pack_event_ack(&ack, buf, sizeof(buf));
    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, sizeof(buf));
    return ble_gattc_notify_custom(g_conn_handle, g_char_event_val_handle, om);
}

esp_err_t imb_ble_send_report_chunk(const imb_pkt_report_chunk_t *chunk, const imb_pkt_report_entry_t *entries)
{
    if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE || !g_is_authed) return ESP_ERR_INVALID_STATE;

    uint8_t buf[IMB_PROTO_BUF_MAX];
    size_t len = imb_proto_pack_report_chunk(chunk, entries, buf, sizeof(buf));
    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, len);
    return ble_gattc_notify_custom(g_conn_handle, g_char_report_val_handle, om);
}

void imb_ble_disconnect(void)
{
    if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(g_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}
