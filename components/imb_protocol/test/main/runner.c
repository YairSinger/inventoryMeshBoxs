#include "unity.h"

extern void test_pack_unpack_event_tag(void);
extern void test_pack_unpack_cmd_hello(void);
extern void test_pack_unpack_event_ack(void);
extern void test_pack_unpack_report_chunk(void);
extern void test_unpack_generic_cmd(void);

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_pack_unpack_event_tag);
    RUN_TEST(test_pack_unpack_cmd_hello);
    RUN_TEST(test_pack_unpack_event_ack);
    RUN_TEST(test_pack_unpack_report_chunk);
    RUN_TEST(test_unpack_generic_cmd);
    return UNITY_END();
}
