#include "unity.h"
#include "imb_nfc.h"
#include <string.h>
#include <stddef.h>

/* ── HAL spy ─────────────────────────────────────────────────────────── */

typedef struct {
    /* scan */
    int          scan_call_count;
    uint8_t      scan_reader_id;
    imb_nfc_tag_t scan_result;    /* returned to caller when scan_found=1 */
    int          scan_found;

    /* read_pages */
    int     read_call_count;
    uint8_t read_reader_id;
    uint8_t read_start_page;
    size_t  read_len;
    uint8_t read_data[64];        /* injected bytes returned to caller */
    int     read_ok;

    /* write_pages */
    int     write_call_count;
    uint8_t write_reader_id;
    uint8_t write_start_page;
    uint8_t write_data[64];
    size_t  write_len;
    int     write_ok;
} hal_spy_t;

static hal_spy_t g_spy;

static int spy_scan(uint8_t reader_id, imb_nfc_tag_t *out, void *ctx)
{
    (void)ctx;
    g_spy.scan_call_count++;
    g_spy.scan_reader_id = reader_id;
    if (g_spy.scan_found) *out = g_spy.scan_result;
    out->found = g_spy.scan_found;
    return g_spy.scan_found;
}

static int spy_read_pages(uint8_t reader_id, const imb_nfc_tag_t *tag,
                          uint8_t start_page, uint8_t *out, size_t len, void *ctx)
{
    (void)tag; (void)ctx;
    g_spy.read_call_count++;
    g_spy.read_reader_id  = reader_id;
    g_spy.read_start_page = start_page;
    g_spy.read_len        = len;
    if (g_spy.read_ok && len <= sizeof(g_spy.read_data))
        memcpy(out, g_spy.read_data, len);
    return g_spy.read_ok;
}

static int spy_write_pages(uint8_t reader_id, const imb_nfc_tag_t *tag,
                           uint8_t start_page, const uint8_t *data, size_t len, void *ctx)
{
    (void)tag; (void)ctx;
    g_spy.write_call_count++;
    g_spy.write_reader_id  = reader_id;
    g_spy.write_start_page = start_page;
    g_spy.write_len        = len;
    if (len <= sizeof(g_spy.write_data))
        memcpy(g_spy.write_data, data, len);
    return g_spy.write_ok;
}

static imb_nfc_hal_t make_hal(void)
{
    imb_nfc_hal_t h = {
        .scan        = spy_scan,
        .read_pages  = spy_read_pages,
        .write_pages = spy_write_pages,
        .ctx         = NULL,
    };
    return h;
}

void setUp(void)
{
    memset(&g_spy, 0, sizeof(g_spy));
    imb_nfc_hal_t h = make_hal();
    imb_nfc_init(&h);
}

void tearDown(void) {}

/* ── scan ────────────────────────────────────────────────────────────── */

void test_scan_returns_0_when_no_tag(void)
{
    g_spy.scan_found = 0;
    imb_nfc_tag_t t;
    TEST_ASSERT_EQUAL_INT(0, imb_nfc_scan(0, &t));
    TEST_ASSERT_EQUAL_INT(1, g_spy.scan_call_count);
    TEST_ASSERT_EQUAL_UINT8(0, g_spy.scan_reader_id);
}

void test_scan_returns_1_and_fills_uid_str(void)
{
    g_spy.scan_result.uid_bytes[0] = 0xAB;
    g_spy.scan_result.uid_bytes[1] = 0xCD;
    g_spy.scan_result.uid_bytes[2] = 0xEF;
    g_spy.scan_result.uid_len = 3;
    g_spy.scan_found = 1;

    imb_nfc_tag_t t;
    TEST_ASSERT_EQUAL_INT(1, imb_nfc_scan(0, &t));
    TEST_ASSERT_EQUAL_STRING("ABCDEF", t.uid_str);
}

void test_scan_passes_reader_id(void)
{
    g_spy.scan_found = 0;
    imb_nfc_tag_t t;
    imb_nfc_scan(1, &t);
    TEST_ASSERT_EQUAL_UINT8(1, g_spy.scan_reader_id);
}

void test_scan_uid_str_empty_when_not_found(void)
{
    g_spy.scan_found = 0;
    imb_nfc_tag_t t;
    imb_nfc_scan(0, &t);
    TEST_ASSERT_EQUAL_STRING("", t.uid_str);
}

/* ── find_by_uid ─────────────────────────────────────────────────────── */

void test_find_by_uid_returns_0_when_no_tag(void)
{
    g_spy.scan_found = 0;
    imb_nfc_tag_t t;
    TEST_ASSERT_EQUAL_INT(0, imb_nfc_find_by_uid(0, "AABBCC", &t));
}

void test_find_by_uid_returns_0_on_uid_mismatch(void)
{
    g_spy.scan_result.uid_bytes[0] = 0x11;
    g_spy.scan_result.uid_bytes[1] = 0x22;
    g_spy.scan_result.uid_len = 2;
    g_spy.scan_found = 1;

    imb_nfc_tag_t t;
    TEST_ASSERT_EQUAL_INT(0, imb_nfc_find_by_uid(0, "AABBCC", &t));
}

void test_find_by_uid_returns_1_on_match(void)
{
    g_spy.scan_result.uid_bytes[0] = 0xAA;
    g_spy.scan_result.uid_bytes[1] = 0xBB;
    g_spy.scan_result.uid_len = 2;
    g_spy.scan_found = 1;

    imb_nfc_tag_t t;
    TEST_ASSERT_EQUAL_INT(1, imb_nfc_find_by_uid(0, "AABB", &t));
}

/* ── write_ndef ──────────────────────────────────────────────────────── */

void test_write_ndef_calls_write_pages_at_page_4(void)
{
    g_spy.write_ok = 1;
    imb_nfc_tag_t tag = {.sak = 0x00, .found = 1};
    TEST_ASSERT_EQUAL_INT(1, imb_nfc_write_ndef(0, &tag, "Torch"));
    TEST_ASSERT_EQUAL_INT(1, g_spy.write_call_count);
    TEST_ASSERT_EQUAL_UINT8(4, g_spy.write_start_page);
}

void test_write_ndef_encodes_ndef_tlv(void)
{
    g_spy.write_ok = 1;
    imb_nfc_tag_t tag = {.sak = 0x00, .found = 1};
    imb_nfc_write_ndef(0, &tag, "Hi");

    /* Expect: 0x03 rec_len 0xD1 0x01 payload_len 0x54 0x02 'e' 'n' 'H' 'i' 0xFE */
    TEST_ASSERT_EQUAL_HEX8(0x03, g_spy.write_data[0]); /* TLV type */
    TEST_ASSERT_EQUAL_HEX8(0x54, g_spy.write_data[5]); /* "T" type byte */
    TEST_ASSERT_EQUAL_HEX8('H',  g_spy.write_data[9]);
    TEST_ASSERT_EQUAL_HEX8('i',  g_spy.write_data[10]);
    TEST_ASSERT_EQUAL_HEX8(0xFE, g_spy.write_data[11]); /* terminator */
}

void test_write_ndef_returns_0_on_hal_failure(void)
{
    g_spy.write_ok = 0;
    imb_nfc_tag_t tag = {.sak = 0x00, .found = 1};
    TEST_ASSERT_EQUAL_INT(0, imb_nfc_write_ndef(0, &tag, "Torch"));
}

void test_write_ndef_passes_reader_id(void)
{
    g_spy.write_ok = 1;
    imb_nfc_tag_t tag = {.sak = 0x00, .found = 1};
    imb_nfc_write_ndef(1, &tag, "Torch");
    TEST_ASSERT_EQUAL_UINT8(1, g_spy.write_reader_id);
}

/* ── read_ndef ───────────────────────────────────────────────────────── */

/* Builds a valid NDEF TLV for "Lantern" into buf; returns byte count. */
static size_t make_ndef_raw(const char *text, uint8_t *buf, size_t max)
{
    size_t tlen        = strlen(text);
    size_t payload_len = 1 + 2 + tlen;
    size_t record_len  = 4 + payload_len;
    size_t i = 0;
    buf[i++] = 0x03;
    buf[i++] = (uint8_t)record_len;
    buf[i++] = 0xD1;
    buf[i++] = 0x01;
    buf[i++] = (uint8_t)payload_len;
    buf[i++] = 0x54;
    buf[i++] = 0x02;
    buf[i++] = 0x65; /* 'e' */
    buf[i++] = 0x6E; /* 'n' */
    memcpy(buf + i, text, tlen); i += tlen;
    buf[i++] = 0xFE;
    if (i < max) buf[i] = 0;
    return i;
}

void test_read_ndef_returns_name_from_valid_record(void)
{
    make_ndef_raw("Lantern", g_spy.read_data, sizeof(g_spy.read_data));
    g_spy.read_ok = 1;

    char name[32];
    imb_nfc_tag_t tag = {.sak = 0x00, .found = 1};
    TEST_ASSERT_EQUAL_INT(1, imb_nfc_read_ndef(0, &tag, name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("Lantern", name);
}

void test_read_ndef_reads_from_page_4(void)
{
    make_ndef_raw("X", g_spy.read_data, sizeof(g_spy.read_data));
    g_spy.read_ok = 1;

    char name[32];
    imb_nfc_tag_t tag = {.sak = 0x00, .found = 1};
    imb_nfc_read_ndef(0, &tag, name, sizeof(name));
    TEST_ASSERT_EQUAL_UINT8(4, g_spy.read_start_page);
    TEST_ASSERT_EQUAL_size_t(48, g_spy.read_len);
}

void test_read_ndef_returns_0_on_hal_failure(void)
{
    g_spy.read_ok = 0;
    char name[32];
    imb_nfc_tag_t tag = {.sak = 0x00, .found = 1};
    TEST_ASSERT_EQUAL_INT(0, imb_nfc_read_ndef(0, &tag, name, sizeof(name)));
}

void test_read_ndef_returns_0_on_missing_tlv_marker(void)
{
    memset(g_spy.read_data, 0, sizeof(g_spy.read_data));
    g_spy.read_data[0] = 0x00; /* not 0x03 */
    g_spy.read_ok = 1;

    char name[32];
    imb_nfc_tag_t tag = {.sak = 0x00, .found = 1};
    TEST_ASSERT_EQUAL_INT(0, imb_nfc_read_ndef(0, &tag, name, sizeof(name)));
}

void test_read_ndef_truncates_to_name_max(void)
{
    make_ndef_raw("Longname", g_spy.read_data, sizeof(g_spy.read_data));
    g_spy.read_ok = 1;

    char name[5]; /* only 4 chars + null */
    imb_nfc_tag_t tag = {.sak = 0x00, .found = 1};
    TEST_ASSERT_EQUAL_INT(1, imb_nfc_read_ndef(0, &tag, name, sizeof(name)));
    TEST_ASSERT_EQUAL_INT('\0', name[4]);
    TEST_ASSERT_EQUAL_INT(4, (int)strlen(name));
}
