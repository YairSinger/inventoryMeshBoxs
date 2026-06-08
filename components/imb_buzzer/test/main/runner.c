#include "unity.h"

void test_tag_placed_starts_tone_and_schedules_duration(void);
void test_tag_placed_timer_fires_silence_and_no_reschedule(void);
void test_item_removed_plays_two_beeps_with_gap(void);
void test_unknown_tag_plays_long_beep(void);
void test_error_plays_three_rapid_beeps(void);
void test_ble_connected_plays_rising_chirp(void);
void test_factory_reset_starts_continuous_tone_no_schedule(void);
void test_buzzer_silence_calls_hal_silence_and_cancel(void);

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_tag_placed_starts_tone_and_schedules_duration);
    RUN_TEST(test_tag_placed_timer_fires_silence_and_no_reschedule);
    RUN_TEST(test_item_removed_plays_two_beeps_with_gap);
    RUN_TEST(test_unknown_tag_plays_long_beep);
    RUN_TEST(test_error_plays_three_rapid_beeps);
    RUN_TEST(test_ble_connected_plays_rising_chirp);
    RUN_TEST(test_factory_reset_starts_continuous_tone_no_schedule);
    RUN_TEST(test_buzzer_silence_calls_hal_silence_and_cancel);
    return UNITY_END();
}
