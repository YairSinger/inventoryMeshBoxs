#include "unity.h"

void test_empty_session_against_full_registry_all_MISSING(void);
void test_ambiguous_item_is_AMBIGUOUS_name_filled_if_registered(void);
void test_session_item_not_in_registry_is_FOREIGN(void);
void test_registered_item_absent_from_session_is_MISSING(void);
void test_registered_item_in_session_is_PRESENT_with_name(void);

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_session_against_full_registry_all_MISSING);
    RUN_TEST(test_ambiguous_item_is_AMBIGUOUS_name_filled_if_registered);
    RUN_TEST(test_session_item_not_in_registry_is_FOREIGN);
    RUN_TEST(test_registered_item_absent_from_session_is_MISSING);
    RUN_TEST(test_registered_item_in_session_is_PRESENT_with_name);
    return UNITY_END();
}
