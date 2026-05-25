#include "unity.h"
#include "imb_protocol.h"
#include <string.h>

void setUp(void)    {}
void tearDown(void) {}

/* ── tests ─────────────────────────────────────────────────────────────── */

void test_round_trip_event_tag_pack_unpack_same_data(void)
{
    imb_pkt_event_tag_t original = {
        .msg_type  = IMB_MSG_EVENT_TAG,
        .direction = IMB_EXTRACT,
        .uid       = "BBBBBBBBBBBB02",
        .name      = "stove",
    };

    uint8_t buf[sizeof(imb_pkt_event_tag_t)];
    size_t  n = imb_proto_pack_event_tag(&original, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(sizeof(imb_pkt_event_tag_t), n);

    imb_pkt_event_tag_t result;
    memset(&result, 0, sizeof(result));
    TEST_ASSERT_EQUAL_INT(0, imb_proto_unpack_event_tag(buf, n, &result));

    TEST_ASSERT_EQUAL_UINT8(IMB_MSG_EVENT_TAG, result.msg_type);
    TEST_ASSERT_EQUAL_UINT8(IMB_EXTRACT,       result.direction);
    TEST_ASSERT_EQUAL_STRING("BBBBBBBBBBBB02", result.uid);
    TEST_ASSERT_EQUAL_STRING("stove",          result.name);
}

void test_unpack_cmd_name_populates_struct(void)
{
    imb_pkt_cmd_name_t original = { .msg_type = IMB_MSG_CMD_NAME };
    strncpy(original.uid,  "04A32F123456EF", IMB_UID_LEN  - 1);
    strncpy(original.name, "tent",           IMB_NAME_LEN - 1);

    uint8_t buf[sizeof(imb_pkt_cmd_name_t)];
    imb_proto_pack_cmd_name(&original, buf, sizeof(buf));

    imb_cmd_u out;
    TEST_ASSERT_EQUAL_INT(0, imb_proto_unpack_cmd(buf, sizeof(buf), &out));
    TEST_ASSERT_EQUAL_UINT8(IMB_MSG_CMD_NAME, out.msg_type);
    TEST_ASSERT_EQUAL_STRING("04A32F123456EF", out.name.uid);
    TEST_ASSERT_EQUAL_STRING("tent",           out.name.name);
}

void test_unpack_cmd_mode_populates_struct(void)
{
    imb_pkt_cmd_mode_t original = {
        .msg_type = IMB_MSG_CMD_MODE,
        .mode     = IMB_MODE_REGISTRATION,
    };
    uint8_t buf[sizeof(imb_pkt_cmd_mode_t)];
    imb_proto_pack_cmd_mode(&original, buf, sizeof(buf));

    imb_cmd_u out;
    TEST_ASSERT_EQUAL_INT(0, imb_proto_unpack_cmd(buf, sizeof(buf), &out));
    TEST_ASSERT_EQUAL_UINT8(IMB_MSG_CMD_MODE,      out.msg_type);
    TEST_ASSERT_EQUAL_UINT8(IMB_MODE_REGISTRATION, out.mode.mode);
}

void test_pack_report_produces_correct_bytes(void)
{
    imb_pkt_report_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_type = IMB_MSG_REPORT;
    msg.count    = 2;
    msg.entries[0].box_id = 0;
    msg.entries[0].status = IMB_DELTA_PRESENT;
    strncpy(msg.entries[0].uid,  "AAAAAAAAAAAA01", IMB_UID_LEN  - 1);
    strncpy(msg.entries[0].name, "tent",           IMB_NAME_LEN - 1);
    msg.entries[1].box_id = 0;
    msg.entries[1].status = IMB_DELTA_MISSING;
    strncpy(msg.entries[1].uid,  "BBBBBBBBBBBB02", IMB_UID_LEN  - 1);
    strncpy(msg.entries[1].name, "stove",          IMB_NAME_LEN - 1);

    uint8_t buf[IMB_PROTO_BUF_MAX];
    size_t  expected_size = sizeof(msg.msg_type) + sizeof(msg.count) +
                            2 * sizeof(imb_pkt_report_entry_t);
    size_t  n = imb_proto_pack_report(&msg, buf, sizeof(buf));

    TEST_ASSERT_EQUAL_size_t(expected_size, n);
    TEST_ASSERT_EQUAL_UINT8(IMB_MSG_REPORT, buf[0]);

    /* count is uint16_t at offset 1 — read via memcpy to stay portable */
    uint16_t count;
    memcpy(&count, buf + 1, sizeof(count));
    TEST_ASSERT_EQUAL_UINT16(2, count);

    /* first entry starts at offset 3 */
    const imb_pkt_report_entry_t *e0 = (const imb_pkt_report_entry_t *)(buf + 3);
    TEST_ASSERT_EQUAL_UINT8(IMB_DELTA_PRESENT, e0->status);
    TEST_ASSERT_EQUAL_STRING("AAAAAAAAAAAA01", e0->uid);
    TEST_ASSERT_EQUAL_STRING("tent",           e0->name);
}

void test_pack_event_tag_produces_correct_bytes(void)
{
    imb_pkt_event_tag_t msg = {
        .msg_type  = IMB_MSG_EVENT_TAG,
        .direction = IMB_INSERT,
        .uid       = "04A32F123456EF",
        .name      = "tent",
    };

    uint8_t buf[sizeof(imb_pkt_event_tag_t)];
    size_t  n = imb_proto_pack_event_tag(&msg, buf, sizeof(buf));

    TEST_ASSERT_EQUAL_size_t(sizeof(imb_pkt_event_tag_t), n);
    TEST_ASSERT_EQUAL_UINT8(IMB_MSG_EVENT_TAG, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(IMB_INSERT,        buf[1]);
    TEST_ASSERT_EQUAL_STRING("04A32F123456EF", (char *)(buf + 2));
    TEST_ASSERT_EQUAL_STRING("tent",           (char *)(buf + 2 + IMB_UID_LEN));
}
