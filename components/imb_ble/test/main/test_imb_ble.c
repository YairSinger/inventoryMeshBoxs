#include "unity.h"
#include "imb_ble.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/*
 * IMB_BLE UUIDs — service base e5d50000-01d0-47e0-afc5-01e466d9298e
 * Byte arrays are little-endian (NimBLE BLE_UUID128_DECLARE order).
 */
static const ble_uuid128_t k_svc_uuid = BLE_UUID128_INIT(
    0x8e, 0x29, 0xd9, 0x66, 0xe4, 0x01, 0xc5, 0xaf,
    0xe0, 0x47, 0xd0, 0x01, 0x00, 0x00, 0xd5, 0xe5);

static const ble_uuid128_t k_event_uuid = BLE_UUID128_INIT(
    0x8e, 0x29, 0xd9, 0x66, 0xe4, 0x01, 0xc5, 0xaf,
    0xe0, 0x47, 0xd0, 0x01, 0x01, 0x00, 0xd5, 0xe5);

static const ble_uuid128_t k_report_uuid = BLE_UUID128_INIT(
    0x8e, 0x29, 0xd9, 0x66, 0xe4, 0x01, 0xc5, 0xaf,
    0xe0, 0x47, 0xd0, 0x01, 0x02, 0x00, 0xd5, 0xe5);

static const ble_uuid128_t k_cmd_uuid = BLE_UUID128_INIT(
    0x8e, 0x29, 0xd9, 0x66, 0xe4, 0x01, 0xc5, 0xaf,
    0xe0, 0x47, 0xd0, 0x01, 0x03, 0x00, 0xd5, 0xe5);

/* ── Stub callbacks ─────────────────────────────────────────────────────── */

static void on_connected(void *ctx)    { (void)ctx; }
static void on_subscribed(void *ctx)   { (void)ctx; }
static void on_cmd(void *ctx, const uint8_t *buf, size_t len)
    { (void)ctx; (void)buf; (void)len; }
static void on_disconnected(void *ctx) { (void)ctx; }

/* ── Helpers ────────────────────────────────────────────────────────────── */

/* Wait until NimBLE sync fires and advertising starts (max timeout_ms). */
static bool wait_adv_active(int timeout_ms)
{
    for (int i = 0; i < timeout_ms / 10; i++) {
        if (ble_gap_adv_active()) return true;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return false;
}

/* ── Tests ──────────────────────────────────────────────────────────────── */

void test_gatt_service_registered(void)
{
    uint16_t start_handle;
    int rc = ble_gatts_find_svc(&k_svc_uuid.u, &start_handle);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, "IMB service not found in GATT server");
    TEST_ASSERT_NOT_EQUAL(0, start_handle);
}

void test_event_notify_characteristic_present(void)
{
    uint16_t def_handle, val_handle;
    int rc = ble_gatts_find_chr(&k_svc_uuid.u, &k_event_uuid.u,
                                &def_handle, &val_handle);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, "EVENT_NOTIFY characteristic not found");
    TEST_ASSERT_NOT_EQUAL(0, val_handle);
}

void test_report_notify_characteristic_present(void)
{
    uint16_t def_handle, val_handle;
    int rc = ble_gatts_find_chr(&k_svc_uuid.u, &k_report_uuid.u,
                                &def_handle, &val_handle);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, "REPORT_NOTIFY characteristic not found");
    TEST_ASSERT_NOT_EQUAL(0, val_handle);
}

void test_command_write_characteristic_present(void)
{
    uint16_t def_handle, val_handle;
    int rc = ble_gatts_find_chr(&k_svc_uuid.u, &k_cmd_uuid.u,
                                &def_handle, &val_handle);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, "COMMAND_WRITE characteristic not found");
    TEST_ASSERT_NOT_EQUAL(0, val_handle);
}

void test_advertising_active_after_sync(void)
{
    /* start_advertising() is called from on_sync; if adv is active the
       entire init path completed successfully. */
    TEST_ASSERT_MESSAGE(ble_gap_adv_active(), "Advertising not active after sync");
}

/* ── Entry point ────────────────────────────────────────────────────────── */

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    const imb_ble_callbacks_t cbs = {
        .on_connected    = on_connected,
        .on_subscribed   = on_subscribed,
        .on_cmd          = on_cmd,
        .on_disconnected = on_disconnected,
    };

    esp_err_t err = imb_ble_init(&cbs, NULL);
    if (err != ESP_OK) {
        printf("FAIL: imb_ble_init returned %d\n", err);
        return;
    }

    /* Wait up to 3 s for NimBLE host task to sync and start advertising */
    if (!wait_adv_active(3000)) {
        printf("FAIL: NimBLE stack did not sync within 3 s\n");
        return;
    }

    UNITY_BEGIN();
    RUN_TEST(test_gatt_service_registered);
    RUN_TEST(test_event_notify_characteristic_present);
    RUN_TEST(test_report_notify_characteristic_present);
    RUN_TEST(test_command_write_characteristic_present);
    RUN_TEST(test_advertising_active_after_sync);
    UNITY_END();
}
