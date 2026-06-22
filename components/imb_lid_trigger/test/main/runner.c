#include "unity.h"

void test_boot_open_seeds_stable_state_immediately(void);
void test_boot_closed_seeds_stable_state_immediately(void);
void test_open_edge_publishes_after_debounce_window(void);
void test_bounce_back_to_stable_state_is_ignored(void);
void test_close_edge_publishes_after_debounce_window(void);

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_boot_open_seeds_stable_state_immediately);
    RUN_TEST(test_boot_closed_seeds_stable_state_immediately);
    RUN_TEST(test_open_edge_publishes_after_debounce_window);
    RUN_TEST(test_bounce_back_to_stable_state_is_ignored);
    RUN_TEST(test_close_edge_publishes_after_debounce_window);
    return UNITY_END();
}
