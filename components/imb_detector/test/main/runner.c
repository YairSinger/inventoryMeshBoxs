#include "unity.h"

/* forward declarations */
void test_reader0_then_reader1_within_window_fires_INSERT(void);
void test_reader1_then_reader0_within_window_fires_EXTRACT(void);
void test_only_reader0_fires_window_expires_fires_AMBIGUOUS(void);
void test_only_reader1_fires_window_expires_fires_AMBIGUOUS(void);
void test_second_reader_fires_after_window_fires_AMBIGUOUS_then_new_pending(void);

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_reader0_then_reader1_within_window_fires_INSERT);
    RUN_TEST(test_reader1_then_reader0_within_window_fires_EXTRACT);
    RUN_TEST(test_only_reader0_fires_window_expires_fires_AMBIGUOUS);
    RUN_TEST(test_only_reader1_fires_window_expires_fires_AMBIGUOUS);
    RUN_TEST(test_second_reader_fires_after_window_fires_AMBIGUOUS_then_new_pending);
    return UNITY_END();
}
