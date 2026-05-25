#include "unity.h"

void test_round_trip_event_tag_pack_unpack_same_data(void);
void test_unpack_cmd_name_populates_struct(void);
void test_unpack_cmd_mode_populates_struct(void);
void test_pack_report_produces_correct_bytes(void);
void test_pack_event_tag_produces_correct_bytes(void);

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_round_trip_event_tag_pack_unpack_same_data);
    RUN_TEST(test_unpack_cmd_name_populates_struct);
    RUN_TEST(test_unpack_cmd_mode_populates_struct);
    RUN_TEST(test_pack_report_produces_correct_bytes);
    RUN_TEST(test_pack_event_tag_produces_correct_bytes);
    return UNITY_END();
}
