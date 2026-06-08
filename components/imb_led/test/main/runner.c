#include "unity.h"

void test_tag_insert_sets_green_and_schedules_duration(void);
void test_tag_insert_timer_fires_turns_off(void);
void test_reg_fail_double_flash_with_gap(void);
void test_ble_idle_repeats_dim_white_pulse(void);
void test_sleep_sets_off_immediately_no_schedule(void);
void test_stop_cancels_and_turns_off(void);
void test_breathing_no_dark_flash_between_steps(void);

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_tag_insert_sets_green_and_schedules_duration);
    RUN_TEST(test_tag_insert_timer_fires_turns_off);
    RUN_TEST(test_reg_fail_double_flash_with_gap);
    RUN_TEST(test_ble_idle_repeats_dim_white_pulse);
    RUN_TEST(test_sleep_sets_off_immediately_no_schedule);
    RUN_TEST(test_stop_cancels_and_turns_off);
    RUN_TEST(test_breathing_no_dark_flash_between_steps);
    return UNITY_END();
}
