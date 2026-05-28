#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"

#define PIN_MOSI  11
#define PIN_MISO  13
#define PIN_SCK   12
#define PIN_CS1   10
#define PIN_CS2    9

#define PN532_SPI_STATREAD  0x02
#define PN532_SPI_DATAWRITE 0x01
#define PN532_SPI_DATAREAD  0x03

static const uint8_t cmd_get_fw[] = {
    0x00, 0x00, 0xFF, 0x02, 0xFE, 0xD4, 0x02, 0x2A, 0x00
};

/* SAMConfiguration: NormalMode, no timeout, no IRQ.
 * Must be sent after power-on before any RF command or the RF field stays inactive. */
static const uint8_t cmd_sam_config[] = {
    0x00, 0x00, 0xFF, 0x05, 0xFB,
    0xD4, 0x14, 0x01, 0x00, 0x00,
    0x17, 0x00
};

/* RFConfiguration: MaxRetries item (0x05), MxRtyPassiveActivation=3.
 * Limits InListPassiveTarget to 3 attempts so it returns quickly when
 * no tag is present instead of blocking indefinitely. */
static const uint8_t cmd_rfconfig_retries[] = {
    0x00, 0x00, 0xFF, 0x06, 0xFA,
    0xD4, 0x32, 0x05, 0xFF, 0xFF, 0x03,
    0xF4, 0x00
};

/* InListPassiveTarget: MaxTg=1, BrTy=0x00 (106 kbps ISO14443A) */
static const uint8_t cmd_14443a[] = {
    0x00, 0x00, 0xFF, 0x04, 0xFC,
    0xD4, 0x4A, 0x01, 0x00,
    0xE1, 0x00
};

/* bit-bang SPI, LSB-first, mode 0 — PN532 is natively LSB-first */
static uint8_t bb_byte(uint8_t out)
{
    uint8_t in = 0;
    for (int i = 0; i < 8; i++) {
        gpio_set_level(PIN_MOSI, (out >> i) & 1);
        ets_delay_us(2);
        gpio_set_level(PIN_SCK, 1);
        ets_delay_us(2);
        if (gpio_get_level(PIN_MISO)) in |= (1 << i);
        gpio_set_level(PIN_SCK, 0);
        ets_delay_us(2);
    }
    return in;
}

static void bb_write(int cs_pin, const uint8_t *buf, size_t len)
{
    gpio_set_level(cs_pin, 0);
    ets_delay_us(5);
    for (size_t i = 0; i < len; i++) bb_byte(buf[i]);
    ets_delay_us(5);
    gpio_set_level(cs_pin, 1);
}

static uint8_t bb_read_ex(int cs_pin, uint8_t cmd, uint8_t *rx, size_t len)
{
    gpio_set_level(cs_pin, 0);
    ets_delay_us(5);
    uint8_t opcode_miso = bb_byte(cmd);
    for (size_t i = 0; i < len; i++) rx[i] = bb_byte(0x00);
    ets_delay_us(5);
    gpio_set_level(cs_pin, 1);
    return opcode_miso;
}

static void bb_read(int cs_pin, uint8_t cmd, uint8_t *rx, size_t len)
{
    bb_read_ex(cs_pin, cmd, rx, len);
}

/* Poll STATREAD until ready, with a bounded retry count and explicit yield. */
static int wait_ready(int cs_pin, int max_tries, int delay_ms)
{
    for (int i = 0; i < max_tries; i++) {
        uint8_t s = 0;
        bb_read(cs_pin, PN532_SPI_STATREAD, &s, 1);
        if (s == 0x01) return 1;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    return 0;
}

/* Wakeup pulse: CS low for ≥ tOSC_START (max 2ms). 10ms gives margin;
 * 5ms gap after lets the PN532 oscillator settle before the next transaction. */
static void wakeup(int cs_pin)
{
    gpio_set_level(cs_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(cs_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(5));
}

static void probe(int cs_pin, int num)
{
    printf("[PN532 #%d] == probe start ==\n", num);

    wakeup(cs_pin);

    uint8_t wbuf[sizeof(cmd_get_fw) + 1];
    wbuf[0] = PN532_SPI_DATAWRITE;
    memcpy(wbuf + 1, cmd_get_fw, sizeof(cmd_get_fw));
    bb_write(cs_pin, wbuf, sizeof(wbuf));
    printf("[PN532 #%d] command sent\n", num);

    int ready = 0;
    for (int i = 0; i < 100; i++) {
        uint8_t opcode_miso = 0, status = 0;
        opcode_miso = bb_read_ex(cs_pin, PN532_SPI_STATREAD, &status, 1);
        if (status == 0x01) { ready = 1; break; }
        if (opcode_miso == 0x01) {
            uint8_t ack_tail[5] = {0};
            bb_read(cs_pin, PN532_SPI_DATAREAD, ack_tail, 5);
            ready = 2;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (ready == 1) {
        uint8_t ack[6] = {0};
        bb_read(cs_pin, PN532_SPI_DATAREAD, ack, 6);
    }

    if (!wait_ready(cs_pin, 100, 5)) {
        printf("[PN532 #%d] FAIL — response timeout\n", num);
        return;
    }

    uint8_t resp[20] = {0};
    bb_read(cs_pin, PN532_SPI_DATAREAD, resp, 20);

    for (int i = 0; i < 18; i++) {
        if (resp[i] == 0xD5 && resp[i+1] == 0x03) {
            printf("[PN532 #%d] OK  IC=0x%02X  Ver=%d  Rev=%d\n",
                   num, resp[i+2], resp[i+3], resp[i+4]);
            return;
        }
    }
    printf("[PN532 #%d] FAIL — D5 03 not found\n", num);
}

static void send_cmd_read_ack_resp(int cs_pin, const uint8_t *cmd, size_t cmd_len,
                                   uint8_t *resp, size_t resp_len)
{
    uint8_t wbuf[64];
    wbuf[0] = PN532_SPI_DATAWRITE;
    memcpy(wbuf + 1, cmd, cmd_len);
    bb_write(cs_pin, wbuf, cmd_len + 1);

    if (!wait_ready(cs_pin, 50, 10)) return;
    uint8_t ack[6] = {0};
    bb_read(cs_pin, PN532_SPI_DATAREAD, ack, 6);

    if (!wait_ready(cs_pin, 50, 10)) return;
    bb_read(cs_pin, PN532_SPI_DATAREAD, resp, resp_len);
}

static void sam_config(int cs_pin)
{
    uint8_t resp[8] = {0};
    send_cmd_read_ack_resp(cs_pin, cmd_sam_config, sizeof(cmd_sam_config), resp, sizeof(resp));
}

static void rfconfig_retries(int cs_pin)
{
    uint8_t resp[8] = {0};
    send_cmd_read_ack_resp(cs_pin, cmd_rfconfig_retries, sizeof(cmd_rfconfig_retries), resp, sizeof(resp));
}

/* Returns 1 if a tag was found and printed, 0 if no tag in field. */
static int scan_tag(int cs_pin, int num)
{
    wakeup(cs_pin);

    uint8_t wbuf[sizeof(cmd_14443a) + 1];
    wbuf[0] = PN532_SPI_DATAWRITE;
    memcpy(wbuf + 1, cmd_14443a, sizeof(cmd_14443a));
    bb_write(cs_pin, wbuf, sizeof(wbuf));

    if (!wait_ready(cs_pin, 50, 10)) return 0;
    uint8_t ack[6] = {0};
    bb_read(cs_pin, PN532_SPI_DATAREAD, ack, 6);

    if (!wait_ready(cs_pin, 50, 10)) return 0;

    uint8_t resp[32] = {0};
    bb_read(cs_pin, PN532_SPI_DATAREAD, resp, sizeof(resp));

    for (int i = 0; i < 28; i++) {
        if (resp[i] == 0xD5 && resp[i+1] == 0x4B) {
            if (resp[i+2] == 0) return 0;
            uint8_t uid_len = resp[i+7];
            printf("[PN532 #%d] TAG  ATQA=%02X%02X SAK=%02X UID(%d):",
                   num, resp[i+4], resp[i+5], resp[i+6], uid_len);
            for (int j = 0; j < uid_len && j < 10; j++)
                printf(" %02X", resp[i+8+j]);
            printf("\n");
            return 1;
        }
    }
    return 0;
}

void app_main(void)
{
    printf("\n=== IMB Phase 1 — PN532 bit-bang SPI + tag scan ===\n");

    gpio_config_t out_cfg = {
        .pin_bit_mask  = (1ULL << PIN_MOSI) | (1ULL << PIN_SCK)
                       | (1ULL << PIN_CS1)  | (1ULL << PIN_CS2),
        .mode          = GPIO_MODE_OUTPUT,
    };
    gpio_config(&out_cfg);

    gpio_config_t in_cfg = {
        .pin_bit_mask  = (1ULL << PIN_MISO),
        .mode          = GPIO_MODE_INPUT,
        .pull_up_en    = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&in_cfg);

    gpio_set_level(PIN_CS1, 1);
    gpio_set_level(PIN_CS2, 1);

    vTaskDelay(pdMS_TO_TICKS(1000));
    probe(PIN_CS1, 1);
    probe(PIN_CS2, 2);

    sam_config(PIN_CS1);
    sam_config(PIN_CS2);

    rfconfig_retries(PIN_CS1);
    rfconfig_retries(PIN_CS2);

    printf("=== Scanning for tags... ===\n");

    while (1) {
        scan_tag(PIN_CS1, 1);
        scan_tag(PIN_CS2, 2);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
