#include "unity.h"

void test_cmd_before_hello_gets_not_authed_ack(void);
void test_hello_correct_pin_sends_ack_ok_and_authenticates(void);
void test_hello_wrong_pin_sends_mismatch_and_disconnects(void);
void test_queued_events_flushed_on_subscribed(void);
void test_ninth_push_drops_oldest_and_event_dropped_sent_on_flush(void);
void test_field_check_to_registration_persists_and_fires_app_callback(void);
void test_invalid_mode_transition_sends_invalid_mode_ack(void);
void test_mode_to_field_check_blocked_when_pending_uids(void);
void test_grace_timeout_with_no_pending_uids_transitions_to_field_check(void);
void test_grace_timeout_with_pending_uids_transitions_to_incomplete_and_persists(void);
void test_deliver_report_sends_correct_number_of_chunks(void);
void test_report_nack_retransmits_requested_chunk(void);

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_cmd_before_hello_gets_not_authed_ack);
    RUN_TEST(test_hello_correct_pin_sends_ack_ok_and_authenticates);
    RUN_TEST(test_hello_wrong_pin_sends_mismatch_and_disconnects);
    RUN_TEST(test_queued_events_flushed_on_subscribed);
    RUN_TEST(test_ninth_push_drops_oldest_and_event_dropped_sent_on_flush);
    RUN_TEST(test_field_check_to_registration_persists_and_fires_app_callback);
    RUN_TEST(test_invalid_mode_transition_sends_invalid_mode_ack);
    RUN_TEST(test_mode_to_field_check_blocked_when_pending_uids);
    RUN_TEST(test_grace_timeout_with_no_pending_uids_transitions_to_field_check);
    RUN_TEST(test_grace_timeout_with_pending_uids_transitions_to_incomplete_and_persists);
    RUN_TEST(test_deliver_report_sends_correct_number_of_chunks);
    RUN_TEST(test_report_nack_retransmits_requested_chunk);
    return UNITY_END();
}
