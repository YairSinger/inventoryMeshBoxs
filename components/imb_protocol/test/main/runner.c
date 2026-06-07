#include "unity.h"

void test_round_trip_cmd_hello_preserves_msg_id_and_pin_hash(void);
void test_round_trip_event_ack_preserves_all_fields(void);
void test_round_trip_report_chunk_header_and_entries(void);
void test_pack_event_tag_produces_correct_bytes(void);
void test_round_trip_event_tag_pack_unpack_same_data(void);
void test_unpack_cmd_mode_populates_struct_with_msg_id(void);
void test_unpack_cmd_name_populates_struct_with_msg_id(void);
void test_unpack_cmd_accept_populates_struct_with_msg_id(void);
void test_event_ack_carries_all_status_codes(void);

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_round_trip_cmd_hello_preserves_msg_id_and_pin_hash);
    RUN_TEST(test_round_trip_event_ack_preserves_all_fields);
    RUN_TEST(test_round_trip_report_chunk_header_and_entries);
    RUN_TEST(test_pack_event_tag_produces_correct_bytes);
    RUN_TEST(test_round_trip_event_tag_pack_unpack_same_data);
    RUN_TEST(test_unpack_cmd_mode_populates_struct_with_msg_id);
    RUN_TEST(test_unpack_cmd_name_populates_struct_with_msg_id);
    RUN_TEST(test_unpack_cmd_accept_populates_struct_with_msg_id);
    RUN_TEST(test_event_ack_carries_all_status_codes);
    return UNITY_END();
}
