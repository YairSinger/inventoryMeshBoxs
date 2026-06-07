#include "imb_ble_session.h"
#include <string.h>

/* ── Internal constants ─────────────────────────────────────────────────── */

#define EVENT_QUEUE_DEPTH  8
#define EVENT_SLOT_SIZE    sizeof(imb_pkt_event_tag_t)
#define PENDING_UIDS_MAX   IMB_REGISTRY_MAX_ITEMS

/* ── Internal state ─────────────────────────────────────────────────────── */

typedef struct {
    /* packed EVENT_TAG bytes; slot 0 = oldest */
    uint8_t  slots[EVENT_QUEUE_DEPTH][EVENT_SLOT_SIZE];
    uint8_t  sizes[EVENT_QUEUE_DEPTH];
    uint8_t  head;   /* index of next slot to read */
    uint8_t  count;
    uint8_t  drop_count;
} event_queue_t;

typedef struct {
    /* Config */
    imb_ble_session_config_t cfg;

    /* Connection state */
    bool is_authed;
    bool is_subscribed;

    /* Event queue */
    event_queue_t queue;

    /* Mode */
    imb_op_mode_e mode;

    /* Pending UIDs (REGISTRATION in-progress) */
    char    pending_uids[PENDING_UIDS_MAX][IMB_UID_LEN];
    uint8_t pending_count;

    /* In-flight name op: msg_id → uid */
    uint8_t inflight_msg_id;
    char    inflight_uid[IMB_UID_LEN];
    bool    inflight_active;

    /* Report delivery state */
    imb_pkt_report_entry_t report_entries[IMB_REPORT_MAX_ENTRIES];
    uint16_t report_count;
    uint16_t report_id;
    uint16_t report_next_chunk;
    uint16_t report_chunk_total;
    bool     report_active;

    /* Timer callbacks (stored for timer HAL) */
    imb_session_timer_cb_t hello_cb;
    imb_session_timer_cb_t grace_cb;
} session_state_t;

static session_state_t g_s;

/* ── Helpers ────────────────────────────────────────────────────────────── */

static void send_ack(uint8_t msg_id, uint8_t msg_type, imb_ack_status_e status)
{
    imb_pkt_event_ack_t pkt = {
        .msg_type       = IMB_MSG_EVENT_ACK,
        .acked_msg_id   = msg_id,
        .acked_msg_type = msg_type,
        .status         = (uint8_t)status,
    };
    uint8_t buf[sizeof(pkt)];
    size_t  n = imb_proto_pack_event_ack(&pkt, buf, sizeof(buf));
    if (g_s.cfg.ble && g_s.cfg.ble->notify_event)
        g_s.cfg.ble->notify_event(buf, n, g_s.cfg.ble->ctx);
}

static void queue_push(const uint8_t *packed, uint8_t len)
{
    if (g_s.queue.count == EVENT_QUEUE_DEPTH) {
        /* Drop oldest, advance head */
        g_s.queue.head = (g_s.queue.head + 1) % EVENT_QUEUE_DEPTH;
        g_s.queue.count--;
        g_s.queue.drop_count++;
    }
    uint8_t tail = (g_s.queue.head + g_s.queue.count) % EVENT_QUEUE_DEPTH;
    memcpy(g_s.queue.slots[tail], packed, len);
    g_s.queue.sizes[tail] = len;
    g_s.queue.count++;
}

static void queue_flush(void)
{
    if (!g_s.cfg.ble || !g_s.cfg.ble->notify_event) return;

    /* If events were dropped, prepend a single EVENT_DROPPED notification */
    if (g_s.queue.drop_count > 0) {
        uint8_t dropped[2] = { IMB_MSG_EVENT_DROPPED, g_s.queue.drop_count };
        g_s.cfg.ble->notify_event(dropped, sizeof(dropped), g_s.cfg.ble->ctx);
        g_s.queue.drop_count = 0;
    }

    while (g_s.queue.count > 0) {
        uint8_t *slot = g_s.queue.slots[g_s.queue.head];
        uint8_t  size = g_s.queue.sizes[g_s.queue.head];
        g_s.cfg.ble->notify_event(slot, size, g_s.cfg.ble->ctx);
        g_s.queue.head = (g_s.queue.head + 1) % EVENT_QUEUE_DEPTH;
        g_s.queue.count--;
    }
}

static bool pending_uid_remove(const char *uid)
{
    for (uint8_t i = 0; i < g_s.pending_count; i++) {
        if (memcmp(g_s.pending_uids[i], uid, IMB_UID_LEN) == 0) {
            /* Swap with last */
            g_s.pending_count--;
            if (i < g_s.pending_count)
                memcpy(g_s.pending_uids[i], g_s.pending_uids[g_s.pending_count], IMB_UID_LEN);
            return true;
        }
    }
    return false;
}

static bool is_valid_mode_transition(imb_op_mode_e from, imb_op_mode_e to)
{
    switch (from) {
    case IMB_MODE_SETUP:
        return false;  /* Only CMD_SET_PIN can leave SETUP */
    case IMB_MODE_FIELD_CHECK:
        return to == IMB_MODE_REGISTRATION;
    case IMB_MODE_REGISTRATION:
        return to == IMB_MODE_FIELD_CHECK;
    case IMB_MODE_REGISTRATION_INCOMPLETE:
        return false;  /* Phone cannot directly set this; only grace window can */
    default:
        return false;
    }
}

/* Send the next pending report chunk */
static void report_send_chunk(uint16_t chunk_index)
{
    if (!g_s.report_active || !g_s.cfg.ble || !g_s.cfg.ble->notify_report) return;

    uint16_t offset = chunk_index * IMB_SESSION_ENTRIES_PER_CHUNK;
    if (offset >= g_s.report_count) return;

    uint16_t entries_in_chunk = g_s.report_count - offset;
    if (entries_in_chunk > IMB_SESSION_ENTRIES_PER_CHUNK)
        entries_in_chunk = IMB_SESSION_ENTRIES_PER_CHUNK;

    imb_pkt_report_chunk_t hdr = {
        .msg_type         = IMB_MSG_REPORT_CHUNK,
        .report_id        = g_s.report_id,
        .chunk_index      = chunk_index,
        .chunk_total      = g_s.report_chunk_total,
        .entries_in_chunk = entries_in_chunk,
    };

    uint8_t buf[IMB_PROTO_BUF_MAX];
    size_t  n = imb_proto_pack_report_chunk(&hdr, g_s.report_entries + offset, buf, sizeof(buf));
    g_s.cfg.ble->notify_report(buf, n, g_s.cfg.ble->ctx);
}

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static void send_event_mode(imb_op_mode_e mode)
{
    imb_pkt_event_mode_t pkt = { .msg_type = IMB_MSG_EVENT_MODE, .mode = (uint8_t)mode };
    uint8_t buf[sizeof(pkt)];
    size_t  n = imb_proto_pack_event_mode(&pkt, buf, sizeof(buf));
    if (g_s.cfg.ble && g_s.cfg.ble->notify_event)
        g_s.cfg.ble->notify_event(buf, n, g_s.cfg.ble->ctx);
}

/* ── Timer callbacks ────────────────────────────────────────────────────── */

static void hello_timeout_cb(void *arg)
{
    (void)arg;
    if (!g_s.is_authed) {
        if (g_s.cfg.ble && g_s.cfg.ble->disconnect)
            g_s.cfg.ble->disconnect(g_s.cfg.ble->ctx);
    }
}

static void grace_timeout_cb(void *arg)
{
    (void)arg;
    if (g_s.pending_count > 0) {
        g_s.mode = IMB_MODE_REGISTRATION_INCOMPLETE;
        if (g_s.cfg.nvs && g_s.cfg.nvs->write_op_mode)
            g_s.cfg.nvs->write_op_mode(g_s.mode, g_s.cfg.nvs->ctx);
        if (g_s.cfg.nvs && g_s.cfg.nvs->write_pending_uids)
            g_s.cfg.nvs->write_pending_uids(
                (const char (*)[IMB_UID_LEN])g_s.pending_uids,
                g_s.pending_count, g_s.cfg.nvs->ctx);
    } else {
        g_s.mode = IMB_MODE_FIELD_CHECK;
        if (g_s.cfg.nvs && g_s.cfg.nvs->write_op_mode)
            g_s.cfg.nvs->write_op_mode(g_s.mode, g_s.cfg.nvs->ctx);
    }
}

/* ── Command handlers ───────────────────────────────────────────────────── */

static void handle_hello(const uint8_t *buf, size_t len)
{
    imb_pkt_cmd_hello_t hello;
    if (imb_proto_unpack_cmd_hello(buf, len, &hello) != 0) return;

    if (g_s.cfg.hello_timer.stop)
        g_s.cfg.hello_timer.stop(g_s.cfg.hello_timer.ctx);

    if (hello.pin_hash == g_s.cfg.pin_hash) {
        g_s.is_authed = true;
        send_ack(hello.msg_id, IMB_MSG_CMD_HELLO, IMB_ACK_OK);
    } else {
        send_ack(hello.msg_id, IMB_MSG_CMD_HELLO, IMB_ACK_PIN_MISMATCH);
        if (g_s.cfg.ble && g_s.cfg.ble->disconnect)
            g_s.cfg.ble->disconnect(g_s.cfg.ble->ctx);
    }
}

static void handle_mode(const uint8_t *buf, size_t len)
{
    imb_pkt_cmd_mode_t cmd;
    if (imb_proto_unpack_cmd(buf, len, &cmd) != 0) return;
    imb_op_mode_e target = (imb_op_mode_e)cmd.mode;

    /* Transition to FIELD_CHECK from REGISTRATION with pending UIDs → INCOMPLETE */
    if (g_s.mode == IMB_MODE_REGISTRATION &&
        target == IMB_MODE_FIELD_CHECK &&
        g_s.pending_count > 0) {
        send_ack(cmd.msg_id, IMB_MSG_CMD_MODE, IMB_ACK_REGISTRATION_INCOMPLETE);
        return;
    }

    if (!is_valid_mode_transition(g_s.mode, target)) {
        send_ack(cmd.msg_id, IMB_MSG_CMD_MODE, IMB_ACK_INVALID_MODE);
        return;
    }

    g_s.mode = target;
    if (g_s.cfg.nvs && g_s.cfg.nvs->write_op_mode)
        g_s.cfg.nvs->write_op_mode(g_s.mode, g_s.cfg.nvs->ctx);

    send_ack(cmd.msg_id, IMB_MSG_CMD_MODE, IMB_ACK_OK);
    send_event_mode(g_s.mode);

    if (g_s.cfg.app && g_s.cfg.app->on_mode_set)
        g_s.cfg.app->on_mode_set(g_s.cfg.app->ctx, g_s.mode, cmd.msg_id);
}

static void handle_name(const uint8_t *buf, size_t len)
{
    imb_pkt_cmd_name_t cmd;
    if (imb_proto_unpack_cmd(buf, len, &cmd) != 0) return;

    g_s.inflight_active  = true;
    g_s.inflight_msg_id  = cmd.msg_id;
    memcpy(g_s.inflight_uid, cmd.uid, IMB_UID_LEN);

    if (g_s.cfg.app && g_s.cfg.app->on_name_tag)
        g_s.cfg.app->on_name_tag(g_s.cfg.app->ctx, cmd.uid, cmd.msg_id);
}

static void handle_accept(const uint8_t *buf, size_t len)
{
    imb_pkt_cmd_accept_t cmd;
    if (imb_proto_unpack_cmd(buf, len, &cmd) != 0) return;

    if (g_s.cfg.app && g_s.cfg.app->on_accept_tag)
        g_s.cfg.app->on_accept_tag(g_s.cfg.app->ctx, cmd.uid, cmd.accepted, cmd.msg_id);
}

static void handle_set_pin(const uint8_t *buf, size_t len)
{
    if (len < 2) return;
    uint8_t msg_id = buf[1];

    if (g_s.mode != IMB_MODE_SETUP) {
        send_ack(msg_id, IMB_MSG_CMD_SET_PIN, IMB_ACK_INVALID_MODE);
        return;
    }

    imb_pkt_cmd_set_pin_t cmd;
    if (imb_proto_unpack_cmd_set_pin(buf, len, &cmd) != 0) return;

    g_s.cfg.pin_hash = cmd.pin_hash;  /* update so future CMD_HELLO checks use new hash */
    g_s.mode = IMB_MODE_FIELD_CHECK;
    if (g_s.cfg.nvs && g_s.cfg.nvs->write_op_mode)
        g_s.cfg.nvs->write_op_mode(g_s.mode, g_s.cfg.nvs->ctx);

    send_ack(cmd.msg_id, IMB_MSG_CMD_SET_PIN, IMB_ACK_OK);
    send_event_mode(IMB_MODE_FIELD_CHECK);

    /* App must persist pin_hash + box_name to NVS and call imb_ble_update_adv() */
    if (g_s.cfg.app && g_s.cfg.app->on_set_pin)
        g_s.cfg.app->on_set_pin(g_s.cfg.app->ctx, cmd.pin_hash, cmd.box_name, cmd.msg_id);
}

static void handle_unbond(const uint8_t *buf, size_t len)
{
    if (len < 2) return;
    uint8_t msg_id = buf[1];
    send_ack(msg_id, IMB_MSG_CMD_UNBOND, IMB_ACK_OK);
    if (g_s.cfg.ble && g_s.cfg.ble->unbond)
        g_s.cfg.ble->unbond(g_s.cfg.ble->ctx);
    if (g_s.cfg.ble && g_s.cfg.ble->disconnect)
        g_s.cfg.ble->disconnect(g_s.cfg.ble->ctx);
}

static void handle_report_ack(const uint8_t *buf, size_t len)
{
    (void)buf; (void)len;
    if (!g_s.report_active) return;
    /* Phone ACKed the full report */
    g_s.report_active = false;
    if (g_s.cfg.app && g_s.cfg.app->on_report_delivered)
        g_s.cfg.app->on_report_delivered(g_s.cfg.app->ctx, true);
}

static void handle_report_nack(const uint8_t *buf, size_t len)
{
    if (len < 5) return;  /* msg_type + report_id(2) + chunk_index(2) */
    uint16_t chunk_index;
    memcpy(&chunk_index, buf + 3, sizeof(chunk_index));
    report_send_chunk(chunk_index);
}

/* ── Public API ─────────────────────────────────────────────────────────── */

void imb_ble_session_init(const imb_ble_session_config_t *cfg)
{
    memset(&g_s, 0, sizeof(g_s));
    g_s.cfg = *cfg;

    /* Load persisted mode; fall back to SETUP if NVS missing */
    if (cfg->nvs && cfg->nvs->read_op_mode) {
        imb_op_mode_e stored;
        if (cfg->nvs->read_op_mode(&stored, cfg->nvs->ctx) == 0)
            g_s.mode = stored;
    }

    /* Load pending UIDs if in REGISTRATION_INCOMPLETE */
    if (g_s.mode == IMB_MODE_REGISTRATION_INCOMPLETE &&
        cfg->nvs && cfg->nvs->read_pending_uids) {
        cfg->nvs->read_pending_uids(g_s.pending_uids, &g_s.pending_count, cfg->nvs->ctx);
    }
}

void imb_ble_session_on_connected(void *ctx)
{
    (void)ctx;
    g_s.is_authed      = false;
    g_s.is_subscribed  = false;

    /* REGISTRATION_INCOMPLETE → REGISTRATION on reconnect (§3.4 / §6.2) */
    if (g_s.mode == IMB_MODE_REGISTRATION_INCOMPLETE) {
        g_s.mode = IMB_MODE_REGISTRATION;
        if (g_s.cfg.grace_timer.stop)
            g_s.cfg.grace_timer.stop(g_s.cfg.grace_timer.ctx);
        /* Re-queue pending UIDs so they flush to phone on subscribe */
        for (uint8_t i = 0; i < g_s.pending_count; i++) {
            imb_pkt_event_tag_t replay = {
                .msg_type  = IMB_MSG_EVENT_TAG,
                .direction = (uint8_t)IMB_INSERT,
            };
            memcpy(replay.uid, g_s.pending_uids[i], IMB_UID_LEN);
            memset(replay.name, 0, IMB_NAME_LEN);
            uint8_t buf[sizeof(replay)];
            size_t  n = imb_proto_pack_event_tag(&replay, buf, sizeof(buf));
            if (n > 0) queue_push(buf, (uint8_t)n);
        }
    }

    if (g_s.cfg.hello_timer.start)
        g_s.cfg.hello_timer.start(5000, hello_timeout_cb, NULL,
                                  g_s.cfg.hello_timer.ctx);
}

void imb_ble_session_on_subscribed(void *ctx)
{
    (void)ctx;
    g_s.is_subscribed = true;
    queue_flush();
}

void imb_ble_session_on_cmd(void *ctx, const uint8_t *buf, size_t len)
{
    (void)ctx;
    if (len < 2) return;

    uint8_t msg_type = buf[0];
    uint8_t msg_id   = buf[1];

    /* Auth gate */
    if (!g_s.is_authed && msg_type != IMB_MSG_CMD_HELLO) {
        send_ack(msg_id, msg_type, IMB_ACK_NOT_AUTHED);
        return;
    }

    switch (msg_type) {
    case IMB_MSG_CMD_HELLO:        handle_hello(buf, len);       break;
    case IMB_MSG_CMD_MODE:         handle_mode(buf, len);        break;
    case IMB_MSG_CMD_NAME:         handle_name(buf, len);        break;
    case IMB_MSG_CMD_ACCEPT:       handle_accept(buf, len);      break;
    case IMB_MSG_CMD_SET_PIN:      handle_set_pin(buf, len);     break;
    case IMB_MSG_CMD_REPORT_ACK:   handle_report_ack(buf, len);  break;
    case IMB_MSG_CMD_REPORT_NACK:  handle_report_nack(buf, len); break;
    case IMB_MSG_CMD_UNBOND:       handle_unbond(buf, len);      break;
    default: break;  /* CMD_GET_LOG, CMD_MESH_STATUS — not yet implemented */
    }
}

void imb_ble_session_on_disconnected(void *ctx)
{
    (void)ctx;

    /* Stop HELLO timer */
    if (g_s.cfg.hello_timer.stop)
        g_s.cfg.hello_timer.stop(g_s.cfg.hello_timer.ctx);

    /* Start grace window if disconnected during REGISTRATION while authed */
    if (g_s.is_authed && g_s.mode == IMB_MODE_REGISTRATION) {
        if (g_s.cfg.grace_timer.start)
            g_s.cfg.grace_timer.start(60000, grace_timeout_cb, NULL,
                                      g_s.cfg.grace_timer.ctx);
    }

    g_s.is_authed    = false;
    g_s.is_subscribed = false;
}

void imb_ble_session_ack(uint8_t msg_id, imb_ack_status_e status)
{
    uint8_t cmd_type = 0;

    if (g_s.inflight_active && g_s.inflight_msg_id == msg_id) {
        cmd_type = IMB_MSG_CMD_NAME;
        if (status == IMB_ACK_OK)
            pending_uid_remove(g_s.inflight_uid);
        g_s.inflight_active = false;
    }

    send_ack(msg_id, cmd_type, status);
}

int imb_ble_session_push_event_tag(const imb_pkt_event_tag_t *event)
{
    uint8_t buf[EVENT_SLOT_SIZE];
    size_t  n = imb_proto_pack_event_tag(event, buf, sizeof(buf));
    if (n == 0) return -1;

    /* Track as pending if in REGISTRATION and tag has no name (new/unnamed) */
    if (g_s.mode == IMB_MODE_REGISTRATION && event->name[0] == '\0') {
        bool already = false;
        for (uint8_t i = 0; i < g_s.pending_count; i++) {
            if (memcmp(g_s.pending_uids[i], event->uid, IMB_UID_LEN) == 0) {
                already = true;
                break;
            }
        }
        if (!already && g_s.pending_count < PENDING_UIDS_MAX)
            memcpy(g_s.pending_uids[g_s.pending_count++], event->uid, IMB_UID_LEN);
    }

    if (g_s.is_subscribed && g_s.is_authed) {
        /* Online: send immediately */
        if (g_s.cfg.ble && g_s.cfg.ble->notify_event)
            g_s.cfg.ble->notify_event(buf, n, g_s.cfg.ble->ctx);
    } else {
        queue_push(buf, (uint8_t)n);
    }
    return 0;
}

int imb_ble_session_deliver_report(const imb_pkt_report_entry_t *entries, uint16_t count)
{
    if (count > IMB_REPORT_MAX_ENTRIES) return -1;

    memcpy(g_s.report_entries, entries, count * sizeof(imb_pkt_report_entry_t));
    g_s.report_count = count;
    g_s.report_id++;
    g_s.report_next_chunk  = 0;
    g_s.report_chunk_total =
        (count + IMB_SESSION_ENTRIES_PER_CHUNK - 1) / IMB_SESSION_ENTRIES_PER_CHUNK;
    if (g_s.report_chunk_total == 0) g_s.report_chunk_total = 1;
    g_s.report_active = true;

    /* Send all chunks in sequence */
    for (uint16_t i = 0; i < g_s.report_chunk_total; i++)
        report_send_chunk(i);

    return 0;
}
