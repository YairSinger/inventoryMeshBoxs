#include "unity.h"

void test_add_item_can_be_retrieved_by_uid(void);
void test_registry_survives_simulated_nvs_reboot(void);
void test_get_all_returns_full_list(void);
void test_remove_item_no_longer_retrievable(void);
void test_add_duplicate_uid_overwrites_count_unchanged(void);

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_add_item_can_be_retrieved_by_uid);
    RUN_TEST(test_registry_survives_simulated_nvs_reboot);
    RUN_TEST(test_get_all_returns_full_list);
    RUN_TEST(test_remove_item_no_longer_retrievable);
    RUN_TEST(test_add_duplicate_uid_overwrites_count_unchanged);
    return UNITY_END();
}
