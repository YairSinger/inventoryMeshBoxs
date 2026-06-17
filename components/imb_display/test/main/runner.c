#include "unity.h"

void test_field_check_idle_draws_context(void);
void test_setup_idle_draws_mode_and_mac(void);
void test_registration_idle_draws_pending_count(void);
void test_registration_incomplete_idle_draws_error(void);
void test_named_detection_draws_event_and_schedules_5s(void);
void test_event_timer_fires_returns_to_idle(void);
void test_extract_direction_shown_differently(void);
void test_anonymous_detection_draws_unknown_tag_error(void);
void test_ambiguous_named_draws_error_with_name(void);
void test_ambiguous_anonymous_draws_error_anonymous_label(void);
void test_error_held_update_ctx_does_not_redraw(void);
void test_named_detection_clears_error(void);
void test_report_cycles_missing_foreign_ambiguous_then_idle(void);
void test_empty_report_returns_to_idle_immediately(void);

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_field_check_idle_draws_context);
    RUN_TEST(test_setup_idle_draws_mode_and_mac);
    RUN_TEST(test_registration_idle_draws_pending_count);
    RUN_TEST(test_registration_incomplete_idle_draws_error);
    RUN_TEST(test_named_detection_draws_event_and_schedules_5s);
    RUN_TEST(test_event_timer_fires_returns_to_idle);
    RUN_TEST(test_extract_direction_shown_differently);
    RUN_TEST(test_anonymous_detection_draws_unknown_tag_error);
    RUN_TEST(test_ambiguous_named_draws_error_with_name);
    RUN_TEST(test_ambiguous_anonymous_draws_error_anonymous_label);
    RUN_TEST(test_error_held_update_ctx_does_not_redraw);
    RUN_TEST(test_named_detection_clears_error);
    RUN_TEST(test_report_cycles_missing_foreign_ambiguous_then_idle);
    RUN_TEST(test_empty_report_returns_to_idle_immediately);
    return UNITY_END();
}
