#include "unity.h"
#include "imb_ble_session.h"
#include <string.h>
#include <stdio.h>

/* ── Test doubles ────────────────────────────────────────────────────────── */

/* BLE HAL spy */
typedef struct {
    uint8_t  event_bufs[32][256];
    size_t   event_lens[32];
    int      event_call_count;
    uint8_t  report_bufs[32][256];
    size_t   report_lens[32];
    int      report_call_count;
    int      disconnect_count;
} ble_spy_t;

static ble_spy_t g_ble;

static int spy_notify_event(const uint8_t *buf, size_t len, void *ctx)
{
    ble_spy_t *s = (ble_spy_t *)ctx;
    if (s->event_call_count < 32) {
        memcpy(s->event_bufs[s->event_call_count], buf, len);
        s->event_lens[s->event_call_count] = len;
    }
    s->event_call_count++;
    return 0;
}

static int spy_notify_report(const uint8_t *buf, size_t len, void *ctx)
{
    ble_spy_t *s = (ble_spy_t *)ctx;
    if (s->report_call_count < 32) {
        memcpy(s->report_bufs[s->report_call_count], buf, len);
        s->report_lens[s->report_call_count] = len;
    }
    s->report_call_count++;
    return 0;
}

static void spy_disconnect(void *ctx)
{
    ble_spy_t *s = (ble_spy_t *)ctx;
    s->disconnect_count++;
}

/* NVS HAL spy */
typedef struct {
    imb_op_mode_e stored_mode;
    bool          mode_written;
    char          pending_uids[64][IMB_UID_LEN];
    uint8_t       pending_count;
    bool          pending_written;
} nvs_spy_t;

static nvs_spy_t g_nvs;

static int spy_read_op_mode(imb_op_mode_e *out, void *ctx)
{
    nvs_spy_t *s = (nvs_spy_t *)ctx;
    *out = s->stored_mode;
    return 0;
}

static int spy_write_op_mode(imb_op_mode_e mode, void *ctx)
{
    nvs_spy_t *s = (nvs_spy_t *)ctx;
    s->stored_mode = mode;
    s->mode_written = true;
    return 0;
}

static int spy_read_pending_uids(char uids[][IMB_UID_LEN], uint8_t *count_out, void *ctx)
{
    nvs_spy_t *s = (nvs_spy_t *)ctx;
    memcpy(uids, s->pending_uids, s->pending_count * IMB_UID_LEN);
    *count_out = s->pending_count;
    return 0;
}

static int spy_write_pending_uids(const char uids[][IMB_UID_LEN], uint8_t count, void *ctx)
{
    nvs_spy_t *s = (nvs_spy_t *)ctx;
    memcpy(s->pending_uids, uids, count * IMB_UID_LEN);
    s->pending_count  = count;
    s->pending_written = true;
    return 0;
}

/* Timer HAL spy */
typedef struct {
    bool                    running;
    imb_session_timer_cb_t  cb;
    void                   *arg;
} timer_spy_t;

static timer_spy_t g_hello_timer;
static timer_spy_t g_grace_timer;

static void spy_timer_start(uint32_t ms, imb_session_timer_cb_t cb, void *arg, void *ctx)
{
    (void)ms;
    timer_spy_t *t = (timer_spy_t *)ctx;
    t->running = true;
    t->cb      = cb;
    t->arg     = arg;
}

static void spy_timer_stop(void *ctx)
{
    timer_spy_t *t = (timer_spy_t *)ctx;
    t->running = false;
}

static void fire_timer(timer_spy_t *t)
{
    if (t->running && t->cb) {
        t->running = false;
        t->cb(t->arg);
    }
}

/* App callbacks spy */
typedef struct {
    char     name_tag_uid[IMB_UID_LEN];
    uint8_t  name_tag_msg_id;
    int      name_tag_count;
    imb_op_mode_e mode_set_mode;
    uint8_t  mode_set_msg_id;
    int      mode_set_count;
    bool     report_delivered_success;
    int      report_delivered_count;
} app_spy_t;

static app_spy_t g_app;

static void spy_on_name_tag(void *ctx, const char *uid, uint8_t msg_id)
{
    app_spy_t *s = (app_spy_t *)ctx;
    strncpy(s->name_tag_uid, uid, IMB_UID_LEN - 1);
    s->name_tag_msg_id = msg_id;
    s->name_tag_count++;
}

static void spy_on_mode_set(void *ctx, imb_op_mode_e mode, uint8_t msg_id)
{
    app_spy_t *s = (app_spy_t *)ctx;
    s->mode_set_mode   = mode;
    s->mode_set_msg_id = msg_id;
    s->mode_set_count++;
}

static void spy_on_report_delivered(void *ctx, bool success)
{
    app_spy_t *s = (app_spy_t *)ctx;
    s->report_delivered_success = success;
    s->report_delivered_count++;
}

/* ── Test infrastructure ─────────────────────────────────────────────────── */

static imb_ble_session_ble_hal_t g_ble_hal;
static imb_ble_session_nvs_hal_t g_nvs_hal;
static imb_ble_session_app_cbs_t g_app_cbs;

static void reset_spies(void)
{
    memset(&g_ble,        0, sizeof(g_ble));
    memset(&g_nvs,        0, sizeof(g_nvs));
    memset(&g_app,        0, sizeof(g_app));
    memset(&g_hello_timer, 0, sizeof(g_hello_timer));
    memset(&g_grace_timer, 0, sizeof(g_grace_timer));
}

static void init_session(uint32_t pin_hash, imb_op_mode_e initial_mode)
{
    reset_spies();
    g_nvs.stored_mode = initial_mode;

    g_ble_hal.notify_event  = spy_notify_event;
    g_ble_hal.notify_report = spy_notify_report;
    g_ble_hal.disconnect    = spy_disconnect;
    g_ble_hal.ctx           = &g_ble;

    g_nvs_hal.read_op_mode      = spy_read_op_mode;
    g_nvs_hal.write_op_mode     = spy_write_op_mode;
    g_nvs_hal.read_pending_uids  = spy_read_pending_uids;
    g_nvs_hal.write_pending_uids = spy_write_pending_uids;
    g_nvs_hal.ctx               = &g_nvs;

    g_app_cbs.on_name_tag         = spy_on_name_tag;
    g_app_cbs.on_mode_set         = spy_on_mode_set;
    g_app_cbs.on_report_delivered = spy_on_report_delivered;
    g_app_cbs.ctx                 = &g_app;

    imb_ble_session_config_t cfg = {
        .pin_hash   = pin_hash,
        .nvs        = &g_nvs_hal,
        .ble        = &g_ble_hal,
        .app        = &g_app_cbs,
        .hello_timer = {
            .start = spy_timer_start,
            .stop  = spy_timer_stop,
            .ctx   = &g_hello_timer,
        },
        .grace_timer = {
            .start = spy_timer_start,
            .stop  = spy_timer_stop,
            .ctx   = &g_grace_timer,
        },
    };
    imb_ble_session_init(&cfg);
}

/* Pack + send a CMD to the session */
static void send_hello(uint8_t msg_id, uint32_t pin_hash)
{
    imb_pkt_cmd_hello_t pkt = {
        .msg_type = IMB_MSG_CMD_HELLO,
        .msg_id   = msg_id,
        .pin_hash = pin_hash,
    };
    uint8_t buf[sizeof(pkt)];
    size_t  n = imb_proto_pack_cmd_hello(&pkt, buf, sizeof(buf));
    imb_ble_session_on_cmd(NULL, buf, n);
}

static void send_cmd_mode(uint8_t msg_id, imb_op_mode_e mode)
{
    imb_pkt_cmd_mode_t pkt = {
        .msg_type = IMB_MSG_CMD_MODE,
        .msg_id   = msg_id,
        .mode     = (uint8_t)mode,
    };
    uint8_t buf[sizeof(pkt)];
    size_t  n = imb_proto_pack_cmd_mode(&pkt, buf, sizeof(buf));
    imb_ble_session_on_cmd(NULL, buf, n);
}

static void authenticate(uint32_t pin)
{
    send_hello(1, pin);
    /* Reset the spy call count so tests start from a clean count */
    g_ble.event_call_count = 0;
}

/* Pull the last EVENT_ACK the spy received */
static imb_pkt_event_ack_t last_ack(void)
{
    int idx = g_ble.event_call_count - 1;
    imb_pkt_event_ack_t out;
    imb_proto_unpack_event_ack(g_ble.event_bufs[idx], g_ble.event_lens[idx], &out);
    return out;
}

void setUp(void)    {}
void tearDown(void) {}

/* ── Slice 1: Auth gate ──────────────────────────────────────────────────── */

void test_cmd_before_hello_gets_not_authed_ack(void)
{
    init_session(0xDEADBEEF, IMB_MODE_FIELD_CHECK);
    send_cmd_mode(5, IMB_MODE_REGISTRATION);

    TEST_ASSERT_EQUAL_INT(1, g_ble.event_call_count);
    imb_pkt_event_ack_t ack = last_ack();
    TEST_ASSERT_EQUAL_UINT8(IMB_MSG_EVENT_ACK,  ack.msg_type);
    TEST_ASSERT_EQUAL_UINT8(5,                  ack.acked_msg_id);
    TEST_ASSERT_EQUAL_UINT8(IMB_ACK_NOT_AUTHED, ack.status);
}

/* ── Slice 2: HELLO PIN match ────────────────────────────────────────────── */

void test_hello_correct_pin_sends_ack_ok_and_authenticates(void)
{
    init_session(0xDEADBEEF, IMB_MODE_FIELD_CHECK);
    send_hello(7, 0xDEADBEEF);

    TEST_ASSERT_EQUAL_INT(1, g_ble.event_call_count);
    imb_pkt_event_ack_t ack = last_ack();
    TEST_ASSERT_EQUAL_UINT8(IMB_ACK_OK,         ack.status);
    TEST_ASSERT_EQUAL_UINT8(IMB_MSG_CMD_HELLO,   ack.acked_msg_type);
    TEST_ASSERT_EQUAL_UINT8(7,                   ack.acked_msg_id);
    TEST_ASSERT_EQUAL_INT  (0,                   g_ble.disconnect_count);
}

/* ── Slice 3: HELLO PIN mismatch ─────────────────────────────────────────── */

void test_hello_wrong_pin_sends_mismatch_and_disconnects(void)
{
    init_session(0xDEADBEEF, IMB_MODE_FIELD_CHECK);
    send_hello(3, 0x00000001);

    imb_pkt_event_ack_t ack = last_ack();
    TEST_ASSERT_EQUAL_UINT8(IMB_ACK_PIN_MISMATCH, ack.status);
    TEST_ASSERT_EQUAL_INT  (1,                    g_ble.disconnect_count);
}

/* ── Slice 4: Queue flush on subscribed ──────────────────────────────────── */

void test_queued_events_flushed_on_subscribed(void)
{
    init_session(0xDEADBEEF, IMB_MODE_FIELD_CHECK);
    /* Not subscribed yet: push 3 events */
    for (int i = 0; i < 3; i++) {
        imb_pkt_event_tag_t ev = {
            .msg_type  = IMB_MSG_EVENT_TAG,
            .direction = IMB_INSERT,
        };
        snprintf(ev.uid,  IMB_UID_LEN,  "AAAAAAAAA%05d", i);
        snprintf(ev.name, IMB_NAME_LEN, "item%d", i);
        imb_ble_session_push_event_tag(&ev);
    }

    TEST_ASSERT_EQUAL_INT(0, g_ble.event_call_count);  /* nothing sent yet */
    imb_ble_session_on_subscribed(NULL);
    TEST_ASSERT_EQUAL_INT(3, g_ble.event_call_count);  /* all 3 flushed */

    /* Verify first flushed packet is EVENT_TAG */
    TEST_ASSERT_EQUAL_UINT8(IMB_MSG_EVENT_TAG, g_ble.event_bufs[0][0]);
}

/* ── Slice 5: Drop counter ───────────────────────────────────────────────── */

void test_ninth_push_drops_oldest_and_event_dropped_sent_on_flush(void)
{
    init_session(0xDEADBEEF, IMB_MODE_FIELD_CHECK);
    /* Push 9 events: 1 should be dropped */
    for (int i = 0; i < 9; i++) {
        imb_pkt_event_tag_t ev = {
            .msg_type  = IMB_MSG_EVENT_TAG,
            .direction = IMB_INSERT,
        };
        snprintf(ev.uid,  IMB_UID_LEN,  "AAAAAAAAA%05d", i);
        snprintf(ev.name, IMB_NAME_LEN, "item%d", i);
        imb_ble_session_push_event_tag(&ev);
    }

    imb_ble_session_on_subscribed(NULL);

    /* 1 EVENT_DROPPED + 8 EVENT_TAG = 9 calls */
    TEST_ASSERT_EQUAL_INT(9, g_ble.event_call_count);
    TEST_ASSERT_EQUAL_UINT8(IMB_MSG_EVENT_DROPPED, g_ble.event_bufs[0][0]);
    TEST_ASSERT_EQUAL_UINT8(1,                     g_ble.event_bufs[0][1]);
    TEST_ASSERT_EQUAL_UINT8(IMB_MSG_EVENT_TAG,     g_ble.event_bufs[1][0]);
}

/* ── Slice 6: Valid mode transition ──────────────────────────────────────── */

void test_field_check_to_registration_persists_and_fires_app_callback(void)
{
    init_session(0xDEADBEEF, IMB_MODE_FIELD_CHECK);
    authenticate(0xDEADBEEF);
    g_nvs.mode_written = false;

    send_cmd_mode(2, IMB_MODE_REGISTRATION);

    imb_pkt_event_ack_t ack = last_ack();
    TEST_ASSERT_EQUAL_UINT8(IMB_ACK_OK,            ack.status);
    TEST_ASSERT_TRUE       (g_nvs.mode_written);
    TEST_ASSERT_EQUAL_UINT8(IMB_MODE_REGISTRATION, g_nvs.stored_mode);
    TEST_ASSERT_EQUAL_INT  (1,                     g_app.mode_set_count);
    TEST_ASSERT_EQUAL_UINT8(IMB_MODE_REGISTRATION, g_app.mode_set_mode);
}

/* ── Slice 7: Invalid mode transition ────────────────────────────────────── */

void test_invalid_mode_transition_sends_invalid_mode_ack(void)
{
    init_session(0xDEADBEEF, IMB_MODE_FIELD_CHECK);
    authenticate(0xDEADBEEF);

    /* FIELD_CHECK → SETUP is not a valid transition */
    send_cmd_mode(3, IMB_MODE_SETUP);

    imb_pkt_event_ack_t ack = last_ack();
    TEST_ASSERT_EQUAL_UINT8(IMB_ACK_INVALID_MODE, ack.status);
    TEST_ASSERT_FALSE      (g_nvs.mode_written);
}

/* ── Slice 8: REGISTRATION→FIELD_CHECK blocked when pending UIDs exist ───── */

void test_mode_to_field_check_blocked_when_pending_uids(void)
{
    init_session(0xDEADBEEF, IMB_MODE_REGISTRATION);
    authenticate(0xDEADBEEF);
    imb_ble_session_on_subscribed(NULL);

    /* Push an unnamed tag to create a pending UID */
    imb_pkt_event_tag_t ev = {
        .msg_type  = IMB_MSG_EVENT_TAG,
        .direction = IMB_INSERT,
        .uid       = "04A32F123456EF",
        .name      = "",  /* unnamed = pending */
    };
    imb_ble_session_push_event_tag(&ev);
    g_ble.event_call_count = 0;

    send_cmd_mode(4, IMB_MODE_FIELD_CHECK);

    imb_pkt_event_ack_t ack = last_ack();
    TEST_ASSERT_EQUAL_UINT8(IMB_ACK_REGISTRATION_INCOMPLETE, ack.status);
}

/* ── Slice 9: Grace window → FIELD_CHECK when no pending UIDs ───────────── */

void test_grace_timeout_with_no_pending_uids_transitions_to_field_check(void)
{
    init_session(0xDEADBEEF, IMB_MODE_REGISTRATION);
    authenticate(0xDEADBEEF);

    /* Disconnect during REGISTRATION (no pending UIDs) → grace timer starts */
    imb_ble_session_on_disconnected(NULL);
    TEST_ASSERT_TRUE(g_grace_timer.running);

    g_nvs.mode_written = false;
    fire_timer(&g_grace_timer);

    TEST_ASSERT_EQUAL_UINT8(IMB_MODE_FIELD_CHECK, g_nvs.stored_mode);
    TEST_ASSERT_TRUE       (g_nvs.mode_written);
}

/* ── Slice 10: Grace window → REGISTRATION_INCOMPLETE when pending UIDs ──── */

void test_grace_timeout_with_pending_uids_transitions_to_incomplete_and_persists(void)
{
    init_session(0xDEADBEEF, IMB_MODE_REGISTRATION);
    authenticate(0xDEADBEEF);
    imb_ble_session_on_subscribed(NULL);

    /* Push unnamed tag → creates pending UID */
    imb_pkt_event_tag_t ev = {
        .msg_type  = IMB_MSG_EVENT_TAG,
        .direction = IMB_INSERT,
        .uid       = "04A32F123456EF",
        .name      = "",
    };
    imb_ble_session_push_event_tag(&ev);

    imb_ble_session_on_disconnected(NULL);
    TEST_ASSERT_TRUE(g_grace_timer.running);

    g_nvs.mode_written    = false;
    g_nvs.pending_written = false;
    fire_timer(&g_grace_timer);

    TEST_ASSERT_EQUAL_UINT8(IMB_MODE_REGISTRATION_INCOMPLETE, g_nvs.stored_mode);
    TEST_ASSERT_TRUE       (g_nvs.mode_written);
    TEST_ASSERT_TRUE       (g_nvs.pending_written);
    TEST_ASSERT_EQUAL_UINT8(1, g_nvs.pending_count);
    TEST_ASSERT_EQUAL_STRING("04A32F123456EF", g_nvs.pending_uids[0]);
}

/* ── Slice 11: Report chunking ───────────────────────────────────────────── */

void test_deliver_report_sends_correct_number_of_chunks(void)
{
    init_session(0xDEADBEEF, IMB_MODE_FIELD_CHECK);

    /* 5 entries → ceil(5/4) = 2 chunks */
    imb_pkt_report_entry_t entries[5];
    memset(entries, 0, sizeof(entries));
    for (int i = 0; i < 5; i++) {
        entries[i].status = IMB_DELTA_PRESENT;
        snprintf(entries[i].uid,  IMB_UID_LEN,  "AAAAAAAAA%05d", i);
        snprintf(entries[i].name, IMB_NAME_LEN, "item%d", i);
    }

    imb_ble_session_deliver_report(entries, 5);

    TEST_ASSERT_EQUAL_INT(2, g_ble.report_call_count);

    /* First chunk: entries_in_chunk = 4 */
    imb_pkt_report_chunk_t chunk0;
    imb_pkt_report_entry_t entries0[4];
    TEST_ASSERT_EQUAL_INT(0, imb_proto_unpack_report_chunk(
        g_ble.report_bufs[0], g_ble.report_lens[0], &chunk0, entries0));
    TEST_ASSERT_EQUAL_UINT16(0, chunk0.chunk_index);
    TEST_ASSERT_EQUAL_UINT16(2, chunk0.chunk_total);
    TEST_ASSERT_EQUAL_UINT16(4, chunk0.entries_in_chunk);

    /* Second chunk: entries_in_chunk = 1 */
    imb_pkt_report_chunk_t chunk1;
    imb_pkt_report_entry_t entries1[4];
    TEST_ASSERT_EQUAL_INT(0, imb_proto_unpack_report_chunk(
        g_ble.report_bufs[1], g_ble.report_lens[1], &chunk1, entries1));
    TEST_ASSERT_EQUAL_UINT16(1, chunk1.chunk_index);
    TEST_ASSERT_EQUAL_UINT16(1, chunk1.entries_in_chunk);
}

/* ── Slice 12: NACK retransmit ───────────────────────────────────────────── */

void test_report_nack_retransmits_requested_chunk(void)
{
    init_session(0xDEADBEEF, IMB_MODE_FIELD_CHECK);

    imb_pkt_report_entry_t entries[5];
    memset(entries, 0, sizeof(entries));
    for (int i = 0; i < 5; i++) {
        entries[i].status = IMB_DELTA_PRESENT;
        snprintf(entries[i].uid,  IMB_UID_LEN,  "AAAAAAAAA%05d", i);
    }

    imb_ble_session_deliver_report(entries, 5);
    TEST_ASSERT_EQUAL_INT(2, g_ble.report_call_count);

    /* NACK chunk 0 → should resend */
    uint8_t nack[5];
    nack[0] = IMB_MSG_CMD_REPORT_NACK;
    nack[1] = 0; /* msg_id placeholder */
    uint16_t report_id = 0;  /* will be ignored by implementation; use chunk_index */
    memcpy(nack + 2, &report_id, 2);
    uint16_t chunk_index = 0;
    memcpy(nack + 3, &chunk_index, 2);

    /* Authenticate first so session accepts the command */
    authenticate(0xDEADBEEF);
    g_ble.report_call_count = 0;
    imb_ble_session_on_cmd(NULL, nack, sizeof(nack));

    TEST_ASSERT_EQUAL_INT(1, g_ble.report_call_count);
    imb_pkt_report_chunk_t chunk;
    imb_pkt_report_entry_t retx_entries[4];
    imb_proto_unpack_report_chunk(g_ble.report_bufs[0], g_ble.report_lens[0], &chunk, retx_entries);
    TEST_ASSERT_EQUAL_UINT16(0, chunk.chunk_index);
    TEST_ASSERT_EQUAL_UINT16(4, chunk.entries_in_chunk);
}
