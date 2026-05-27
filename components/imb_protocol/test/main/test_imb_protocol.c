#include "unity.h"
#include "imb_protocol.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

void test_pack_unpack_event_tag(void) {
    imb_pkt_event_tag_t msg = {
        .msg_type = IMB_MSG_EVENT_TAG,
        .direction = 1,
        .uid = "04A32F123456EF",
        .name = "tent"
    };
    uint8_t buf[IMB_PROTO_BUF_MAX];
    size_t len = imb_proto_pack_event_tag(&msg, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT32(sizeof(imb_pkt_event_tag_t), len);

    imb_pkt_event_tag_t out;
    TEST_ASSERT_EQUAL_INT(0, imb_proto_unpack_event_tag(buf, len, &out));
    TEST_ASSERT_EQUAL_UINT8(IMB_MSG_EVENT_TAG, out.msg_type);
    TEST_ASSERT_EQUAL_UINT8(1, out.direction);
    TEST_ASSERT_EQUAL_STRING("04A32F123456EF", out.uid);
    TEST_ASSERT_EQUAL_STRING("tent", out.name);
}

void test_pack_unpack_cmd_hello(void) {
    imb_pkt_cmd_hello_t msg = {
        .msg_type = IMB_MSG_CMD_HELLO,
        .msg_id = 42,
        .pin_hash = 0x12345678
    };
    uint8_t buf[IMB_PROTO_BUF_MAX];
    size_t len = imb_proto_pack_cmd_hello(&msg, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT32(sizeof(imb_pkt_cmd_hello_t), len);

    imb_pkt_cmd_hello_t out;
    TEST_ASSERT_EQUAL_INT(0, imb_proto_unpack_cmd_hello(buf, len, &out));
    TEST_ASSERT_EQUAL_UINT8(IMB_MSG_CMD_HELLO, out.msg_type);
    TEST_ASSERT_EQUAL_UINT8(42, out.msg_id);
    TEST_ASSERT_EQUAL_UINT32(0x12345678, out.pin_hash);
}

void test_pack_unpack_event_ack(void) {
    imb_pkt_event_ack_t msg = {
        .msg_type = IMB_MSG_EVENT_ACK,
        .acked_msg_id = 42,
        .acked_msg_type = IMB_MSG_CMD_HELLO,
        .status = IMB_ACK_OK
    };
    uint8_t buf[IMB_PROTO_BUF_MAX];
    size_t len = imb_proto_pack_event_ack(&msg, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT32(4, len);

    imb_pkt_event_ack_t out;
    TEST_ASSERT_EQUAL_INT(0, imb_proto_unpack_event_ack(buf, len, &out));
    TEST_ASSERT_EQUAL_UINT8(IMB_MSG_EVENT_ACK, out.msg_type);
    TEST_ASSERT_EQUAL_UINT8(42, out.acked_msg_id);
    TEST_ASSERT_EQUAL_UINT8(IMB_MSG_CMD_HELLO, out.acked_msg_type);
    TEST_ASSERT_EQUAL_UINT8(IMB_ACK_OK, out.status);
}

void test_pack_unpack_report_chunk(void) {
    imb_pkt_report_chunk_t chunk = {
        .msg_type = IMB_MSG_REPORT_CHUNK,
        .report_id = 1,
        .chunk_index = 0,
        .chunk_total = 1,
        .entries_in_chunk = 2
    };
    imb_pkt_report_entry_t entries[2] = {
        { .box_id = 0, .status = 1, .uid = "UID01", .name = "item1" },
        { .box_id = 0, .status = 2, .uid = "UID02", .name = "item2" }
    };

    uint8_t buf[IMB_PROTO_BUF_MAX];
    size_t len = imb_proto_pack_report_chunk(&chunk, entries, buf, sizeof(buf));
    
    size_t expected_len = sizeof(imb_pkt_report_chunk_t) + (2 * sizeof(imb_pkt_report_entry_t));
    TEST_ASSERT_EQUAL_UINT32(expected_len, len);

    imb_pkt_report_chunk_t out_chunk;
    imb_pkt_report_entry_t out_entries[2];
    TEST_ASSERT_EQUAL_INT(0, imb_proto_unpack_report_chunk(buf, len, &out_chunk, out_entries));
    
    TEST_ASSERT_EQUAL_UINT8(IMB_MSG_REPORT_CHUNK, out_chunk.msg_type);
    TEST_ASSERT_EQUAL_UINT16(2, out_chunk.entries_in_chunk);
    TEST_ASSERT_EQUAL_STRING("UID01", out_entries[0].uid);
    TEST_ASSERT_EQUAL_STRING("item1", out_entries[0].name);
    TEST_ASSERT_EQUAL_STRING("UID02", out_entries[1].uid);
}

void test_unpack_generic_cmd(void) {
    /* Pack a CMD_MODE manually */
    imb_pkt_cmd_mode_t msg = {
        .msg_type = IMB_MSG_CMD_MODE,
        .msg_id = 5,
        .mode = 1
    };
    uint8_t buf[IMB_PROTO_BUF_MAX];
    size_t len = imb_proto_pack_cmd_mode(&msg, buf, sizeof(buf));

    /* Unpack into a generic struct pointer */
    imb_pkt_cmd_mode_t out;
    TEST_ASSERT_EQUAL_INT(0, imb_proto_unpack_cmd(buf, len, &out));
    TEST_ASSERT_EQUAL_UINT8(IMB_MSG_CMD_MODE, out.msg_type);
    TEST_ASSERT_EQUAL_UINT8(5, out.msg_id);
    TEST_ASSERT_EQUAL_UINT8(1, out.mode);
}
