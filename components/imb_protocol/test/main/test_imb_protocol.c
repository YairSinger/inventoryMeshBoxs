#include "unity.h"
#include "imb_protocol.h"
#include <string.h>

void setUp(void)    {}
void tearDown(void) {}

/* ── Slice 1: CMD_HELLO round-trip (tracer bullet) ─────────────────────── */

void test_round_trip_cmd_hello_preserves_msg_id_and_pin_hash(void)
{
    imb_pkt_cmd_hello_t original = {
        .msg_type = IMB_MSG_CMD_HELLO,
        .msg_id   = 42,
        .pin_hash = 0x12345678,
    };

    uint8_t buf[sizeof(imb_pkt_cmd_hello_t)];
    size_t n = imb_proto_pack_cmd_hello(&original, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(sizeof(imb_pkt_cmd_hello_t), n);

    imb_pkt_cmd_hello_t result;
    memset(&result, 0, sizeof(result));
    TEST_ASSERT_EQUAL_INT(0, imb_proto_unpack_cmd_hello(buf, n, &result));

    TEST_ASSERT_EQUAL_UINT8(IMB_MSG_CMD_HELLO, result.msg_type);
    TEST_ASSERT_EQUAL_UINT8(42,                result.msg_id);
    TEST_ASSERT_EQUAL_UINT32(0x12345678,       result.pin_hash);
}

/* ── Slice 2: EVENT_ACK round-trip ─────────────────────────────────────── */

void test_round_trip_event_ack_preserves_all_fields(void)
{
    imb_pkt_event_ack_t original = {
        .msg_type       = IMB_MSG_EVENT_ACK,
        .acked_msg_id   = 7,
        .acked_msg_type = IMB_MSG_CMD_HELLO,
        .status         = IMB_ACK_PIN_MISMATCH,
    };

    uint8_t buf[sizeof(imb_pkt_event_ack_t)];
    size_t n = imb_proto_pack_event_ack(&original, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(4, n);

    imb_pkt_event_ack_t result;
    memset(&result, 0, sizeof(result));
    TEST_ASSERT_EQUAL_INT(0, imb_proto_unpack_event_ack(buf, n, &result));

    TEST_ASSERT_EQUAL_UINT8(IMB_MSG_EVENT_ACK,    result.msg_type);
    TEST_ASSERT_EQUAL_UINT8(7,                    result.acked_msg_id);
    TEST_ASSERT_EQUAL_UINT8(IMB_MSG_CMD_HELLO,    result.acked_msg_type);
    TEST_ASSERT_EQUAL_UINT8(IMB_ACK_PIN_MISMATCH, result.status);
}

/* ── Slice 3: REPORT_CHUNK round-trip ──────────────────────────────────── */

void test_round_trip_report_chunk_header_and_entries(void)
{
    imb_pkt_report_chunk_t hdr = {
        .msg_type        = IMB_MSG_REPORT_CHUNK,
        .report_id       = 1,
        .chunk_index     = 0,
        .chunk_total     = 1,
        .entries_in_chunk = 2,
    };
    imb_pkt_report_entry_t entries[2] = {
        { .box_id = 0, .status = IMB_DELTA_PRESENT,
          .uid = "AAAAAAAAAAAA01", .name = "tent" },
        { .box_id = 0, .status = IMB_DELTA_MISSING,
          .uid = "BBBBBBBBBBBB02", .name = "stove" },
    };

    uint8_t buf[IMB_PROTO_BUF_MAX];
    size_t expected_len = sizeof(imb_pkt_report_chunk_t) + 2 * sizeof(imb_pkt_report_entry_t);
    size_t n = imb_proto_pack_report_chunk(&hdr, entries, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(expected_len, n);

    imb_pkt_report_chunk_t out_hdr;
    imb_pkt_report_entry_t out_entries[2];
    memset(&out_hdr,     0, sizeof(out_hdr));
    memset(out_entries,  0, sizeof(out_entries));
    TEST_ASSERT_EQUAL_INT(0, imb_proto_unpack_report_chunk(buf, n, &out_hdr, out_entries));

    TEST_ASSERT_EQUAL_UINT8 (IMB_MSG_REPORT_CHUNK, out_hdr.msg_type);
    TEST_ASSERT_EQUAL_UINT16(2,                    out_hdr.entries_in_chunk);
    TEST_ASSERT_EQUAL_STRING("AAAAAAAAAAAA01",     out_entries[0].uid);
    TEST_ASSERT_EQUAL_STRING("tent",               out_entries[0].name);
    TEST_ASSERT_EQUAL_UINT8 (IMB_DELTA_MISSING,    out_entries[1].status);
    TEST_ASSERT_EQUAL_STRING("BBBBBBBBBBBB02",     out_entries[1].uid);
}

/* ── Slice 4: EVENT_TAG byte layout (regression) ──────────────────────── */

void test_pack_event_tag_produces_correct_bytes(void)
{
    imb_pkt_event_tag_t msg = {
        .msg_type  = IMB_MSG_EVENT_TAG,
        .direction = IMB_INSERT,
        .uid       = "04A32F123456EF",
        .name      = "tent",
    };

    uint8_t buf[sizeof(imb_pkt_event_tag_t)];
    size_t n = imb_proto_pack_event_tag(&msg, buf, sizeof(buf));

    TEST_ASSERT_EQUAL_size_t(sizeof(imb_pkt_event_tag_t), n);
    TEST_ASSERT_EQUAL_UINT8(IMB_MSG_EVENT_TAG, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(IMB_INSERT,        buf[1]);
    TEST_ASSERT_EQUAL_STRING("04A32F123456EF", (char *)(buf + 2));
    TEST_ASSERT_EQUAL_STRING("tent",           (char *)(buf + 2 + IMB_UID_LEN));
}

void test_round_trip_event_tag_pack_unpack_same_data(void)
{
    imb_pkt_event_tag_t original = {
        .msg_type  = IMB_MSG_EVENT_TAG,
        .direction = IMB_EXTRACT,
        .uid       = "BBBBBBBBBBBB02",
        .name      = "stove",
    };

    uint8_t buf[sizeof(imb_pkt_event_tag_t)];
    size_t n = imb_proto_pack_event_tag(&original, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(sizeof(imb_pkt_event_tag_t), n);

    imb_pkt_event_tag_t result;
    memset(&result, 0, sizeof(result));
    TEST_ASSERT_EQUAL_INT(0, imb_proto_unpack_event_tag(buf, n, &result));

    TEST_ASSERT_EQUAL_UINT8(IMB_MSG_EVENT_TAG, result.msg_type);
    TEST_ASSERT_EQUAL_UINT8(IMB_EXTRACT,       result.direction);
    TEST_ASSERT_EQUAL_STRING("BBBBBBBBBBBB02", result.uid);
    TEST_ASSERT_EQUAL_STRING("stove",          result.name);
}

/* ── Slice 5: CMD_MODE with msg_id (regression + new field) ────────────── */

void test_unpack_cmd_mode_populates_struct_with_msg_id(void)
{
    imb_pkt_cmd_mode_t original = {
        .msg_type = IMB_MSG_CMD_MODE,
        .msg_id   = 3,
        .mode     = IMB_MODE_REGISTRATION,
    };

    uint8_t buf[sizeof(imb_pkt_cmd_mode_t)];
    imb_proto_pack_cmd_mode(&original, buf, sizeof(buf));

    imb_pkt_cmd_mode_t out;
    TEST_ASSERT_EQUAL_INT(0, imb_proto_unpack_cmd(buf, sizeof(buf), &out));
    TEST_ASSERT_EQUAL_UINT8(IMB_MSG_CMD_MODE,      out.msg_type);
    TEST_ASSERT_EQUAL_UINT8(3,                     out.msg_id);
    TEST_ASSERT_EQUAL_UINT8(IMB_MODE_REGISTRATION, out.mode);
}

/* ── Slice 6: CMD_NAME with msg_id (regression + new field) ────────────── */

void test_unpack_cmd_name_populates_struct_with_msg_id(void)
{
    imb_pkt_cmd_name_t original = {
        .msg_type = IMB_MSG_CMD_NAME,
        .msg_id   = 5,
    };
    strncpy(original.uid,  "04A32F123456EF", IMB_UID_LEN  - 1);
    strncpy(original.name, "tent",           IMB_NAME_LEN - 1);

    uint8_t buf[sizeof(imb_pkt_cmd_name_t)];
    imb_proto_pack_cmd_name(&original, buf, sizeof(buf));

    imb_pkt_cmd_name_t out;
    TEST_ASSERT_EQUAL_INT(0, imb_proto_unpack_cmd(buf, sizeof(buf), &out));
    TEST_ASSERT_EQUAL_UINT8(IMB_MSG_CMD_NAME,   out.msg_type);
    TEST_ASSERT_EQUAL_UINT8(5,                  out.msg_id);
    TEST_ASSERT_EQUAL_STRING("04A32F123456EF",  out.uid);
    TEST_ASSERT_EQUAL_STRING("tent",            out.name);
}

/* ── Slice 7: CMD_ACCEPT with msg_id ───────────────────────────────────── */

void test_unpack_cmd_accept_populates_struct_with_msg_id(void)
{
    imb_pkt_cmd_accept_t original = {
        .msg_type = IMB_MSG_CMD_ACCEPT,
        .msg_id   = 9,
        .accepted = 1,
    };
    strncpy(original.uid, "04A32F123456EF", IMB_UID_LEN - 1);

    uint8_t buf[sizeof(imb_pkt_cmd_accept_t)];
    imb_proto_pack_cmd_accept(&original, buf, sizeof(buf));

    imb_pkt_cmd_accept_t out;
    TEST_ASSERT_EQUAL_INT(0, imb_proto_unpack_cmd(buf, sizeof(buf), &out));
    TEST_ASSERT_EQUAL_UINT8(IMB_MSG_CMD_ACCEPT, out.msg_type);
    TEST_ASSERT_EQUAL_UINT8(9,                  out.msg_id);
    TEST_ASSERT_EQUAL_STRING("04A32F123456EF",  out.uid);
    TEST_ASSERT_EQUAL_UINT8(1,                  out.accepted);
}

/* ── Slice 8: ACK status codes ─────────────────────────────────────────── */

void test_event_ack_carries_all_status_codes(void)
{
    imb_ack_status_e codes[] = {
        IMB_ACK_OK,
        IMB_ACK_PIN_MISMATCH,
        IMB_ACK_REGISTRY_FULL,
        IMB_ACK_NDEF_WRITE_FAILED,
        IMB_ACK_INVALID_MODE,
        IMB_ACK_UNKNOWN_UID,
        IMB_ACK_NOT_AUTHED,
        IMB_ACK_REGISTRATION_INCOMPLETE,
    };

    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        imb_pkt_event_ack_t msg = {
            .msg_type       = IMB_MSG_EVENT_ACK,
            .acked_msg_id   = (uint8_t)i,
            .acked_msg_type = IMB_MSG_CMD_MODE,
            .status         = codes[i],
        };
        uint8_t buf[sizeof(imb_pkt_event_ack_t)];
        imb_proto_pack_event_ack(&msg, buf, sizeof(buf));

        imb_pkt_event_ack_t out;
        TEST_ASSERT_EQUAL_INT(0, imb_proto_unpack_event_ack(buf, sizeof(buf), &out));
        TEST_ASSERT_EQUAL_UINT8(codes[i], out.status);
    }
}
