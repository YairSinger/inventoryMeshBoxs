#include "unity.h"
#include "imb_registry.h"
#include <string.h>
#include <stdio.h>

/* ── in-memory NVS stub ────────────────────────────────────────────────── */

#define STUB_MAX_ENTRIES 128
#define STUB_KEY_LEN     32

typedef struct {
    char       key[STUB_KEY_LEN];
    imb_item_t item;
    int        occupied;
} stub_item_entry_t;

typedef struct {
    char     key[STUB_KEY_LEN];
    uint16_t val;
    int      occupied;
} stub_u16_entry_t;

/* static so state persists across imb_registry_init calls — simulates NVS */
static stub_item_entry_t s_items[STUB_MAX_ENTRIES];
static stub_u16_entry_t  s_u16s[STUB_MAX_ENTRIES];

static void stub_nvs_reset(void)
{
    memset(s_items, 0, sizeof(s_items));
    memset(s_u16s,  0, sizeof(s_u16s));
}

static imb_reg_err_e stub_load(const char *key, imb_item_t *out, void *ctx)
{
    (void)ctx;
    for (int i = 0; i < STUB_MAX_ENTRIES; i++) {
        if (s_items[i].occupied && strcmp(s_items[i].key, key) == 0) {
            *out = s_items[i].item;
            return IMB_REG_OK;
        }
    }
    return IMB_REG_ERR_NOT_FOUND;
}

static imb_reg_err_e stub_save(const char *key, const imb_item_t *in, void *ctx)
{
    (void)ctx;
    for (int i = 0; i < STUB_MAX_ENTRIES; i++) {
        if (s_items[i].occupied && strcmp(s_items[i].key, key) == 0) {
            s_items[i].item = *in;
            return IMB_REG_OK;
        }
    }
    for (int i = 0; i < STUB_MAX_ENTRIES; i++) {
        if (!s_items[i].occupied) {
            strncpy(s_items[i].key, key, STUB_KEY_LEN - 1);
            s_items[i].item     = *in;
            s_items[i].occupied = 1;
            return IMB_REG_OK;
        }
    }
    return IMB_REG_ERR_FULL;
}

static imb_reg_err_e stub_erase(const char *key, void *ctx)
{
    (void)ctx;
    for (int i = 0; i < STUB_MAX_ENTRIES; i++) {
        if (s_items[i].occupied && strcmp(s_items[i].key, key) == 0) {
            s_items[i].occupied = 0;
            return IMB_REG_OK;
        }
    }
    return IMB_REG_ERR_NOT_FOUND;
}

static imb_reg_err_e stub_load_u16(const char *key, uint16_t *out, void *ctx)
{
    (void)ctx;
    for (int i = 0; i < STUB_MAX_ENTRIES; i++) {
        if (s_u16s[i].occupied && strcmp(s_u16s[i].key, key) == 0) {
            *out = s_u16s[i].val;
            return IMB_REG_OK;
        }
    }
    return IMB_REG_ERR_NOT_FOUND;
}

static imb_reg_err_e stub_save_u16(const char *key, uint16_t val, void *ctx)
{
    (void)ctx;
    for (int i = 0; i < STUB_MAX_ENTRIES; i++) {
        if (s_u16s[i].occupied && strcmp(s_u16s[i].key, key) == 0) {
            s_u16s[i].val = val;
            return IMB_REG_OK;
        }
    }
    for (int i = 0; i < STUB_MAX_ENTRIES; i++) {
        if (!s_u16s[i].occupied) {
            strncpy(s_u16s[i].key, key, STUB_KEY_LEN - 1);
            s_u16s[i].val      = val;
            s_u16s[i].occupied = 1;
            return IMB_REG_OK;
        }
    }
    return IMB_REG_ERR_FULL;
}

static imb_nvs_hal_t s_hal = {
    .load     = stub_load,
    .save     = stub_save,
    .erase    = stub_erase,
    .load_u16 = stub_load_u16,
    .save_u16 = stub_save_u16,
    .ctx      = NULL,
};

static imb_registry_t make_registry(void)
{
    imb_registry_t reg;
    imb_registry_init(&reg, &s_hal, 64);
    return reg;
}

/* ── setUp / tearDown ──────────────────────────────────────────────────── */

void setUp(void)    { stub_nvs_reset(); }
void tearDown(void) {}

/* ── tests ─────────────────────────────────────────────────────────────── */

void test_add_item_can_be_retrieved_by_uid(void)
{
    imb_registry_t reg = make_registry();
    imb_item_t item = { .uid = "04A32F123456EF", .name = "tent" };

    TEST_ASSERT_EQUAL_INT(IMB_REG_OK, imb_registry_add(&reg, &item));

    imb_item_t out = {0};
    TEST_ASSERT_EQUAL_INT(IMB_REG_OK, imb_registry_get(&reg, "04A32F123456EF", &out));
    TEST_ASSERT_EQUAL_STRING("tent", out.name);
}

void test_registry_survives_simulated_nvs_reboot(void)
{
    /* first boot: add two items */
    {
        imb_registry_t reg = make_registry();
        imb_item_t a = { .uid = "AAAAAAAAAAAA01", .name = "tent" };
        imb_item_t b = { .uid = "BBBBBBBBBBBB02", .name = "stove" };
        imb_registry_add(&reg, &a);
        imb_registry_add(&reg, &b);
    }

    /* second boot: new registry instance, same HAL stub (NVS still intact) */
    {
        imb_registry_t reg;
        imb_registry_init(&reg, &s_hal, 64);

        TEST_ASSERT_EQUAL_UINT16(2, imb_registry_count(&reg));

        imb_item_t out = {0};
        TEST_ASSERT_EQUAL_INT(IMB_REG_OK, imb_registry_get(&reg, "AAAAAAAAAAAA01", &out));
        TEST_ASSERT_EQUAL_STRING("tent", out.name);

        memset(&out, 0, sizeof(out));
        TEST_ASSERT_EQUAL_INT(IMB_REG_OK, imb_registry_get(&reg, "BBBBBBBBBBBB02", &out));
        TEST_ASSERT_EQUAL_STRING("stove", out.name);
    }
}

void test_get_all_returns_full_list(void)
{
    imb_registry_t reg = make_registry();
    imb_item_t a = { .uid = "AAAAAAAAAAAA01", .name = "tent" };
    imb_item_t b = { .uid = "BBBBBBBBBBBB02", .name = "stove" };
    imb_item_t c = { .uid = "CCCCCCCCCCCC03", .name = "lantern" };
    imb_registry_add(&reg, &a);
    imb_registry_add(&reg, &b);
    imb_registry_add(&reg, &c);

    imb_item_t out[IMB_REGISTRY_MAX_ITEMS];
    uint16_t   count = 0;
    TEST_ASSERT_EQUAL_INT(IMB_REG_OK, imb_registry_get_all(&reg, out, &count));
    TEST_ASSERT_EQUAL_UINT16(3, count);
    TEST_ASSERT_EQUAL_STRING("tent",    out[0].name);
    TEST_ASSERT_EQUAL_STRING("stove",   out[1].name);
    TEST_ASSERT_EQUAL_STRING("lantern", out[2].name);
}

void test_remove_item_no_longer_retrievable(void)
{
    imb_registry_t reg = make_registry();
    imb_item_t item = { .uid = "04A32F123456EF", .name = "tent" };
    imb_registry_add(&reg, &item);

    TEST_ASSERT_EQUAL_INT(IMB_REG_OK, imb_registry_remove(&reg, "04A32F123456EF"));

    imb_item_t out = {0};
    TEST_ASSERT_EQUAL_INT(IMB_REG_ERR_NOT_FOUND, imb_registry_get(&reg, "04A32F123456EF", &out));
    TEST_ASSERT_EQUAL_UINT16(0, imb_registry_count(&reg));
}

void test_add_duplicate_uid_overwrites_count_unchanged(void)
{
    imb_registry_t reg = make_registry();
    imb_item_t item = { .uid = "04A32F123456EF", .name = "tent" };
    imb_registry_add(&reg, &item);

    imb_item_t updated = { .uid = "04A32F123456EF", .name = "tarp" };
    TEST_ASSERT_EQUAL_INT(IMB_REG_OK, imb_registry_add(&reg, &updated));

    TEST_ASSERT_EQUAL_UINT16(1, imb_registry_count(&reg));

    imb_item_t out = {0};
    imb_registry_get(&reg, "04A32F123456EF", &out);
    TEST_ASSERT_EQUAL_STRING("tarp", out.name);
}
