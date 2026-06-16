#include "unity.h"

void test_session_reset_clears_all_sets(void);
void test_insert_then_extract_item_not_in_present_set(void);
void test_ambiguous_event_goes_to_ambiguous_set_not_present(void);
void test_extract_event_removes_item_from_present_set(void);
void test_insert_event_adds_item_to_present_set(void);
void test_double_insert_moves_to_ambiguous(void);
void test_extract_with_no_present_record_is_noop(void);
void test_ambiguous_resolved_by_later_insert(void);
void test_ambiguous_resolved_by_later_extract(void);

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_session_reset_clears_all_sets);
    RUN_TEST(test_insert_then_extract_item_not_in_present_set);
    RUN_TEST(test_ambiguous_event_goes_to_ambiguous_set_not_present);
    RUN_TEST(test_extract_event_removes_item_from_present_set);
    RUN_TEST(test_insert_event_adds_item_to_present_set);
    RUN_TEST(test_double_insert_moves_to_ambiguous);
    RUN_TEST(test_extract_with_no_present_record_is_noop);
    RUN_TEST(test_ambiguous_resolved_by_later_insert);
    RUN_TEST(test_ambiguous_resolved_by_later_extract);
    return UNITY_END();
}
