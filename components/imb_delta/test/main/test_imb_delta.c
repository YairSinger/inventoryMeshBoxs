#include "unity.h"
#include "imb_delta.h"
#include <string.h>

/* ── in-memory NVS stub (same pattern as imb_registry tests) ───────────── */

#define STUB_MAX  128
#define STUB_KLEN  32

typedef struct { char key[STUB_KLEN]; imb_item_t item; int used; } stub_item_t;
typedef struct { char key[STUB_KLEN]; uint16_t   val;  int used; } stub_u16_t;

static stub_item_t s_items[STUB_MAX];
static stub_u16_t  s_u16s[STUB_MAX];

static void nvs_reset(void) {
    memset(s_items, 0, sizeof(s_items));
    memset(s_u16s,  0, sizeof(s_u16s));
}

static imb_reg_err_e nvs_load(const char *k, imb_item_t *o, void *c) {
    (void)c;
    for (int i = 0; i < STUB_MAX; i++)
        if (s_items[i].used && strcmp(s_items[i].key, k) == 0) { *o = s_items[i].item; return IMB_REG_OK; }
    return IMB_REG_ERR_NOT_FOUND;
}
static imb_reg_err_e nvs_save(const char *k, const imb_item_t *v, void *c) {
    (void)c;
    for (int i = 0; i < STUB_MAX; i++)
        if (s_items[i].used && strcmp(s_items[i].key, k) == 0) { s_items[i].item = *v; return IMB_REG_OK; }
    for (int i = 0; i < STUB_MAX; i++)
        if (!s_items[i].used) { strncpy(s_items[i].key, k, STUB_KLEN-1); s_items[i].item = *v; s_items[i].used = 1; return IMB_REG_OK; }
    return IMB_REG_ERR_FULL;
}
static imb_reg_err_e nvs_erase(const char *k, void *c) {
    (void)c;
    for (int i = 0; i < STUB_MAX; i++)
        if (s_items[i].used && strcmp(s_items[i].key, k) == 0) { s_items[i].used = 0; return IMB_REG_OK; }
    return IMB_REG_ERR_NOT_FOUND;
}
static imb_reg_err_e nvs_load_u16(const char *k, uint16_t *o, void *c) {
    (void)c;
    for (int i = 0; i < STUB_MAX; i++)
        if (s_u16s[i].used && strcmp(s_u16s[i].key, k) == 0) { *o = s_u16s[i].val; return IMB_REG_OK; }
    return IMB_REG_ERR_NOT_FOUND;
}
static imb_reg_err_e nvs_save_u16(const char *k, uint16_t v, void *c) {
    (void)c;
    for (int i = 0; i < STUB_MAX; i++)
        if (s_u16s[i].used && strcmp(s_u16s[i].key, k) == 0) { s_u16s[i].val = v; return IMB_REG_OK; }
    for (int i = 0; i < STUB_MAX; i++)
        if (!s_u16s[i].used) { strncpy(s_u16s[i].key, k, STUB_KLEN-1); s_u16s[i].val = v; s_u16s[i].used = 1; return IMB_REG_OK; }
    return IMB_REG_ERR_FULL;
}

static imb_nvs_hal_t s_hal = {
    nvs_load, nvs_save, nvs_erase, nvs_load_u16, nvs_save_u16, NULL
};

/* ── helpers ───────────────────────────────────────────────────────────── */

static imb_registry_t make_registry(void)
{
    imb_registry_t reg;
    imb_registry_init(&reg, &s_hal, 64);
    return reg;
}

static void reg_add(imb_registry_t *reg, const char *uid, const char *name)
{
    imb_item_t item;
    strncpy(item.uid,  uid,  IMB_UID_LEN  - 1); item.uid[IMB_UID_LEN  - 1] = '\0';
    strncpy(item.name, name, IMB_NAME_LEN - 1); item.name[IMB_NAME_LEN - 1] = '\0';
    imb_registry_add(reg, &item);
}

static void sess_insert(imb_session_t *s, const char *uid)
{
    imb_scan_event_t e;
    e.dir = IMB_INSERT;
    strncpy(e.uid, uid, sizeof(e.uid) - 1); e.uid[sizeof(e.uid) - 1] = '\0';
    imb_session_apply(s, &e);
}

static void sess_ambiguous(imb_session_t *s, const char *uid)
{
    imb_scan_event_t e;
    e.dir = IMB_AMBIGUOUS;
    strncpy(e.uid, uid, sizeof(e.uid) - 1); e.uid[sizeof(e.uid) - 1] = '\0';
    imb_session_apply(s, &e);
}

static const imb_delta_entry_t *find_entry(const imb_delta_entry_t *out,
                                           uint16_t count, const char *uid)
{
    for (uint16_t i = 0; i < count; i++)
        if (strcmp(out[i].item.uid, uid) == 0) return &out[i];
    return NULL;
}

/* ── setUp / tearDown ──────────────────────────────────────────────────── */

void setUp(void)    { nvs_reset(); }
void tearDown(void) {}

/* ── tests ─────────────────────────────────────────────────────────────── */

void test_empty_session_against_full_registry_all_MISSING(void)
{
    imb_registry_t reg = make_registry();
    reg_add(&reg, "AAAAAAAAAAAA01", "tent");
    reg_add(&reg, "BBBBBBBBBBBB02", "stove");
    reg_add(&reg, "CCCCCCCCCCCC03", "lantern");

    imb_session_t sess;
    imb_session_init(&sess);
    /* lid opened and closed with nothing scanned */

    imb_delta_entry_t out[IMB_DELTA_MAX_ENTRIES];
    uint16_t count = imb_delta_compute(&sess, &reg, out, IMB_DELTA_MAX_ENTRIES);

    TEST_ASSERT_EQUAL_UINT16(3, count);
    for (uint16_t i = 0; i < count; i++)
        TEST_ASSERT_EQUAL_INT(IMB_DELTA_MISSING, out[i].status);
    TEST_ASSERT_NOT_NULL(find_entry(out, count, "AAAAAAAAAAAA01"));
    TEST_ASSERT_NOT_NULL(find_entry(out, count, "BBBBBBBBBBBB02"));
    TEST_ASSERT_NOT_NULL(find_entry(out, count, "CCCCCCCCCCCC03"));
}

void test_ambiguous_item_is_AMBIGUOUS_name_filled_if_registered(void)
{
    imb_registry_t reg = make_registry();
    reg_add(&reg, "04A32F123456EF", "tent");

    imb_session_t sess;
    imb_session_init(&sess);
    sess_ambiguous(&sess, "04A32F123456EF");

    imb_delta_entry_t out[IMB_DELTA_MAX_ENTRIES];
    uint16_t count = imb_delta_compute(&sess, &reg, out, IMB_DELTA_MAX_ENTRIES);

    /* AMBIGUOUS item is not in session present, so registry walk also emits MISSING —
       find the AMBIGUOUS entry specifically */
    const imb_delta_entry_t *e = NULL;
    for (uint16_t i = 0; i < count; i++) {
        if (strcmp(out[i].item.uid, "04A32F123456EF") == 0 &&
            out[i].status == IMB_DELTA_AMBIGUOUS) { e = &out[i]; break; }
    }
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_STRING("tent", e->item.name);
}

void test_session_item_not_in_registry_is_FOREIGN(void)
{
    imb_registry_t reg = make_registry();
    /* registry is empty */

    imb_session_t sess;
    imb_session_init(&sess);
    sess_insert(&sess, "04A32F123456EF");

    imb_delta_entry_t out[IMB_DELTA_MAX_ENTRIES];
    uint16_t count = imb_delta_compute(&sess, &reg, out, IMB_DELTA_MAX_ENTRIES);

    TEST_ASSERT_EQUAL_UINT16(1, count);
    const imb_delta_entry_t *e = find_entry(out, count, "04A32F123456EF");
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_INT(IMB_DELTA_FOREIGN, e->status);
}

void test_registered_item_absent_from_session_is_MISSING(void)
{
    imb_registry_t reg = make_registry();
    reg_add(&reg, "04A32F123456EF", "tent");

    imb_session_t sess;
    imb_session_init(&sess);
    /* nothing inserted */

    imb_delta_entry_t out[IMB_DELTA_MAX_ENTRIES];
    uint16_t count = imb_delta_compute(&sess, &reg, out, IMB_DELTA_MAX_ENTRIES);

    TEST_ASSERT_EQUAL_UINT16(1, count);
    const imb_delta_entry_t *e = find_entry(out, count, "04A32F123456EF");
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_INT(IMB_DELTA_MISSING, e->status);
    TEST_ASSERT_EQUAL_STRING("tent", e->item.name);
}

void test_registered_item_in_session_is_PRESENT_with_name(void)
{
    imb_registry_t reg = make_registry();
    reg_add(&reg, "04A32F123456EF", "tent");

    imb_session_t sess;
    imb_session_init(&sess);
    sess_insert(&sess, "04A32F123456EF");

    imb_delta_entry_t out[IMB_DELTA_MAX_ENTRIES];
    uint16_t count = imb_delta_compute(&sess, &reg, out, IMB_DELTA_MAX_ENTRIES);

    TEST_ASSERT_EQUAL_UINT16(1, count);
    const imb_delta_entry_t *e = find_entry(out, count, "04A32F123456EF");
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_INT(IMB_DELTA_PRESENT, e->status);
    TEST_ASSERT_EQUAL_STRING("tent", e->item.name);
}
