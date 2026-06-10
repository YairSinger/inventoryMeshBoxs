#include "imb_nfc_pn532.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

/* ── Static PN532 frames ─────────────────────────────────────────────── */

static const uint8_t k_sam_config[] = {
    0x00,0x00,0xFF,0x05,0xFB, 0xD4,0x14,0x01,0x00,0x00, 0x17,0x00
};
static const uint8_t k_rfconfig[] = {
    0x00,0x00,0xFF,0x06,0xFA, 0xD4,0x32,0x05,0xFF,0xFF,0x03, 0xF4,0x00
};
static const uint8_t k_14443a[] = {
    0x00,0x00,0xFF,0x04,0xFC, 0xD4,0x4A,0x01,0x00, 0xE1,0x00
};

/* ── Driver context ──────────────────────────────────────────────────── */

typedef struct {
    int mosi, miso, sck;
    int cs[2];
} pn532_ctx_t;

static pn532_ctx_t g_ctx;

/* ── Bit-bang SPI (LSB-first, mode 0) ───────────────────────────────── */

static uint8_t bb_byte(uint8_t out)
{
    uint8_t in = 0;
    for (int i = 0; i < 8; i++) {
        gpio_set_level(g_ctx.mosi, (out >> i) & 1);
        ets_delay_us(2);
        gpio_set_level(g_ctx.sck, 1);
        ets_delay_us(2);
        if (gpio_get_level(g_ctx.miso)) in |= (1 << i);
        gpio_set_level(g_ctx.sck, 0);
        ets_delay_us(2);
    }
    return in;
}

static void bb_write(int cs, const uint8_t *buf, size_t len)
{
    gpio_set_level(cs, 0); ets_delay_us(5);
    for (size_t i = 0; i < len; i++) bb_byte(buf[i]);
    ets_delay_us(5); gpio_set_level(cs, 1);
}

static void bb_read(int cs, uint8_t cmd, uint8_t *rx, size_t len)
{
    gpio_set_level(cs, 0); ets_delay_us(5);
    bb_byte(cmd);
    for (size_t i = 0; i < len; i++) rx[i] = bb_byte(0x00);
    ets_delay_us(5); gpio_set_level(cs, 1);
}

static int wait_ready(int cs, int tries, int delay_ms)
{
    for (int i = 0; i < tries; i++) {
        uint8_t s = 0;
        bb_read(cs, 0x02, &s, 1);
        if (s == 0x01) return 1;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    return 0;
}

/* payload = TFI (0xD4) + command bytes */
static size_t build_frame(uint8_t *out, const uint8_t *payload, size_t plen)
{
    out[0] = 0x00; out[1] = 0x00; out[2] = 0xFF;
    out[3] = (uint8_t)plen;
    out[4] = (uint8_t)(256 - plen);
    memcpy(out + 5, payload, plen);
    uint8_t dcs = 0;
    for (size_t i = 0; i < plen; i++) dcs += payload[i];
    out[5 + plen] = (uint8_t)(256 - dcs);
    out[6 + plen] = 0x00;
    return 7 + plen;
}

static void send_recv(int cs, const uint8_t *frame, size_t flen,
                      uint8_t *resp, size_t rlen)
{
    uint8_t wbuf[64];
    wbuf[0] = 0x01; /* DATAWRITE */
    memcpy(wbuf + 1, frame, flen);
    bb_write(cs, wbuf, flen + 1);
    if (!wait_ready(cs, 50, 10)) return;
    uint8_t ack[6] = {0}; bb_read(cs, 0x03, ack, 6);
    if (!wait_ready(cs, 50, 10)) return;
    bb_read(cs, 0x03, resp, rlen);
}

/* ── Gen1a "magic" MIFARE Classic backdoor ───────────────────────────── */

/* Two-phase backdoor unlock via InCommunicateThru (D4 42). Required because
 * InDataExchange (D4 40) returns Application Error 0x7F on gen1a clone cards. */
static int gen1a_unlock(int cs)
{
    uint8_t payload1[3] = {0xD4, 0x42, 0x40};
    uint8_t frame1[16], resp1[16] = {0};
    send_recv(cs, frame1, build_frame(frame1, payload1, sizeof(payload1)), resp1, sizeof(resp1));
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t payload2[3] = {0xD4, 0x42, 0x43};
    uint8_t frame2[16], resp2[16] = {0};
    send_recv(cs, frame2, build_frame(frame2, payload2, sizeof(payload2)), resp2, sizeof(resp2));

    /* Accept if either phase acked */
    for (int i = 0; i < 14; i++)
        if (resp1[i] == 0xD5 && resp1[i+1] == 0x43) return 1;
    for (int i = 0; i < 14; i++)
        if (resp2[i] == 0xD5 && resp2[i+1] == 0x43) return 1;
    return 1; /* gen1a rarely gives a clean ACK — proceed optimistically */
}

static int is_mifare(uint8_t sak)
{
    return sak == 0x08 || sak == 0x09 || sak == 0x18 || sak == 0x19;
}

/* ── HAL scan ────────────────────────────────────────────────────────── */

static int pn532_scan(uint8_t reader_id, imb_nfc_tag_t *out, void *ctx)
{
    (void)ctx;
    int cs = g_ctx.cs[reader_id & 1];

    uint8_t wbuf[sizeof(k_14443a) + 1];
    wbuf[0] = 0x01;
    memcpy(wbuf + 1, k_14443a, sizeof(k_14443a));
    bb_write(cs, wbuf, sizeof(wbuf));

    if (!wait_ready(cs, 50, 10)) return 0;
    uint8_t ack[6] = {0};  bb_read(cs, 0x03, ack, 6);
    if (!wait_ready(cs, 50, 10)) return 0;
    uint8_t resp[32] = {0}; bb_read(cs, 0x03, resp, sizeof(resp));

    for (int i = 0; i < 28; i++) {
        if (resp[i] == 0xD5 && resp[i+1] == 0x4B && resp[i+2] > 0) {
            out->atqa[0] = resp[i+4]; out->atqa[1] = resp[i+5];
            out->sak     = resp[i+6];
            out->uid_len = resp[i+7]; if (out->uid_len > 10) out->uid_len = 10;
            memcpy(out->uid_bytes, resp + i + 8, out->uid_len);
            out->found   = 1;
            return 1;
        }
    }
    out->found = 0;
    return 0;
}

/* ── HAL read_pages ──────────────────────────────────────────────────── */

/* T2T READ (0x30) returns 4 pages = 16 bytes starting at the given page.
 * We iterate in 16-byte chunks, advancing by 4 pages each time. */
static int t2t_read_pages(int cs, uint8_t start_page, uint8_t *out, size_t len)
{
    for (size_t off = 0; off < len; off += 16) {
        uint8_t page = (uint8_t)(start_page + off / 4);
        uint8_t payload[] = {0xD4, 0x40, 0x01, 0x30, page};
        uint8_t frame[16], resp[32] = {0};
        send_recv(cs, frame, build_frame(frame, payload, sizeof(payload)), resp, sizeof(resp));
        int got = 0;
        for (int i = 0; i < 26; i++) {
            if (resp[i] == 0xD5 && resp[i+1] == 0x41 && resp[i+2] == 0x00) {
                size_t n = (len - off < 16) ? (len - off) : 16;
                memcpy(out + off, resp + i + 3, n);
                got = 1; break;
            }
        }
        if (!got) return 0;
    }
    return 1;
}

/* MIFARE gen1a READ (InCommunicateThru 0x30): 16 bytes per block. */
static int mifare_read_pages(int cs, uint8_t start_page, uint8_t *out, size_t len)
{
    gen1a_unlock(cs);
    for (size_t off = 0; off < len; off += 16) {
        uint8_t block = (uint8_t)(start_page + off / 16);
        uint8_t payload[] = {0xD4, 0x42, 0x30, block};
        uint8_t frame[16], resp[24] = {0};
        send_recv(cs, frame, build_frame(frame, payload, sizeof(payload)), resp, sizeof(resp));
        int got = 0;
        for (int i = 0; i < 20; i++) {
            if (resp[i] == 0xD5 && resp[i+1] == 0x43 && resp[i+2] == 0x00) {
                size_t n = (len - off < 16) ? (len - off) : 16;
                memcpy(out + off, resp + i + 3, n);
                got = 1; break;
            }
        }
        if (!got) return 0;
    }
    return 1;
}

static int pn532_read_pages(uint8_t reader_id, const imb_nfc_tag_t *tag,
                            uint8_t start_page, uint8_t *out, size_t len, void *ctx)
{
    (void)ctx;
    int cs = g_ctx.cs[reader_id & 1];
    if (tag->sak == 0x00)         return t2t_read_pages(cs, start_page, out, len);
    if (is_mifare(tag->sak))      return mifare_read_pages(cs, start_page, out, len);
    return 0;
}

/* ── HAL write_pages ─────────────────────────────────────────────────── */

/* T2T WRITE (0xA2): one page (4 bytes) at a time. */
static int t2t_write_pages(int cs, uint8_t start_page, const uint8_t *data, size_t len)
{
    size_t pages = (len + 3) / 4;
    for (size_t p = 0; p < pages; p++) {
        uint8_t page_data[4] = {0};
        size_t  off = p * 4;
        size_t  n   = (len - off < 4) ? (len - off) : 4;
        memcpy(page_data, data + off, n);

        uint8_t payload[] = {0xD4, 0x40, 0x01, 0xA2, (uint8_t)(start_page + p),
                              page_data[0], page_data[1], page_data[2], page_data[3]};
        uint8_t frame[32], resp[12] = {0};
        send_recv(cs, frame, build_frame(frame, payload, sizeof(payload)), resp, sizeof(resp));
        int ok = 0;
        for (int j = 0; j < 10; j++)
            if (resp[j] == 0xD5 && resp[j+1] == 0x41) { ok = (resp[j+2] == 0); break; }
        if (!ok) return 0;
    }
    return 1;
}

/* MIFARE gen1a WRITE (InCommunicateThru 0xA0): 16 bytes per block. */
static int mifare_write_pages(int cs, uint8_t start_page, const uint8_t *data, size_t len)
{
    gen1a_unlock(cs);
    size_t blocks = (len + 15) / 16;
    for (size_t b = 0; b < blocks; b++) {
        uint8_t block_data[16] = {0};
        size_t  off = b * 16;
        size_t  n   = (len - off < 16) ? (len - off) : 16;
        memcpy(block_data, data + off, n);

        uint8_t wcmd[20];
        wcmd[0] = 0xD4; wcmd[1] = 0x42; wcmd[2] = 0xA0;
        wcmd[3] = (uint8_t)(start_page + b);
        memcpy(wcmd + 4, block_data, 16);
        uint8_t frame[40], resp[16] = {0};
        send_recv(cs, frame, build_frame(frame, wcmd, sizeof(wcmd)), resp, sizeof(resp));

        int ok = 0;
        for (int j = 0; j + 2 < 16; j++)
            if (resp[j] == 0xD5 && resp[j+1] == 0x43 && resp[j+2] == 0x00) { ok = 1; break; }
        if (!ok) return 0;
    }
    return 1;
}

static int pn532_write_pages(uint8_t reader_id, const imb_nfc_tag_t *tag,
                             uint8_t start_page, const uint8_t *data, size_t len, void *ctx)
{
    (void)ctx;
    int cs = g_ctx.cs[reader_id & 1];
    if (tag->sak == 0x00)     return t2t_write_pages(cs, start_page, data, len);
    if (is_mifare(tag->sak))  return mifare_write_pages(cs, start_page, data, len);
    return 0;
}

/* ── Public init ─────────────────────────────────────────────────────── */

imb_nfc_hal_t imb_nfc_pn532_init(const imb_nfc_pn532_config_t *cfg)
{
    g_ctx.mosi = cfg->mosi;
    g_ctx.miso = cfg->miso;
    g_ctx.sck  = cfg->sck;
    g_ctx.cs[0] = cfg->cs[0];
    g_ctx.cs[1] = cfg->cs[1];

    /* GPIO init */
    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << cfg->mosi) | (1ULL << cfg->sck)
                      | (1ULL << cfg->cs[0]) | (1ULL << cfg->cs[1]),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&out_cfg);
    gpio_config_t in_cfg = {
        .pin_bit_mask = (1ULL << cfg->miso),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&in_cfg);
    gpio_set_level(cfg->cs[0], 1);
    gpio_set_level(cfg->cs[1], 1);
    vTaskDelay(pdMS_TO_TICKS(500));

    /* Wakeup + init both readers */
    for (int r = 0; r < 2; r++) {
        int cs = cfg->cs[r];
        gpio_set_level(cs, 0); vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(cs, 1); vTaskDelay(pdMS_TO_TICKS(5));
        uint8_t resp[8] = {0};
        send_recv(cs, k_sam_config, sizeof(k_sam_config), resp, sizeof(resp));
        send_recv(cs, k_rfconfig,   sizeof(k_rfconfig),   resp, sizeof(resp));
    }

    imb_nfc_hal_t hal = {
        .scan        = pn532_scan,
        .read_pages  = pn532_read_pages,
        .write_pages = pn532_write_pages,
        .ctx         = NULL,
    };
    return hal;
}
