#include "unity.h"

void test_scan_returns_0_when_no_tag(void);
void test_scan_returns_1_and_fills_uid_str(void);
void test_scan_passes_reader_id(void);
void test_scan_uid_str_empty_when_not_found(void);
void test_find_by_uid_returns_0_when_no_tag(void);
void test_find_by_uid_returns_0_on_uid_mismatch(void);
void test_find_by_uid_returns_1_on_match(void);
void test_write_ndef_calls_write_pages_at_page_4(void);
void test_write_ndef_encodes_ndef_tlv(void);
void test_write_ndef_returns_0_on_hal_failure(void);
void test_write_ndef_passes_reader_id(void);
void test_read_ndef_returns_name_from_valid_record(void);
void test_read_ndef_reads_from_page_4(void);
void test_read_ndef_returns_0_on_hal_failure(void);
void test_read_ndef_returns_0_on_missing_tlv_marker(void);
void test_read_ndef_truncates_to_name_max(void);

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_scan_returns_0_when_no_tag);
    RUN_TEST(test_scan_returns_1_and_fills_uid_str);
    RUN_TEST(test_scan_passes_reader_id);
    RUN_TEST(test_scan_uid_str_empty_when_not_found);
    RUN_TEST(test_find_by_uid_returns_0_when_no_tag);
    RUN_TEST(test_find_by_uid_returns_0_on_uid_mismatch);
    RUN_TEST(test_find_by_uid_returns_1_on_match);
    RUN_TEST(test_write_ndef_calls_write_pages_at_page_4);
    RUN_TEST(test_write_ndef_encodes_ndef_tlv);
    RUN_TEST(test_write_ndef_returns_0_on_hal_failure);
    RUN_TEST(test_write_ndef_passes_reader_id);
    RUN_TEST(test_read_ndef_returns_name_from_valid_record);
    RUN_TEST(test_read_ndef_reads_from_page_4);
    RUN_TEST(test_read_ndef_returns_0_on_hal_failure);
    RUN_TEST(test_read_ndef_returns_0_on_missing_tlv_marker);
    RUN_TEST(test_read_ndef_truncates_to_name_max);
    return UNITY_END();
}
