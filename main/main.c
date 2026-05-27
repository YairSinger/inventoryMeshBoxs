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

/* bit-bang SPI, LSB-first, mode 0 (PN532 native) */
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

static void bb_read(int cs_pin, uint8_t cmd, uint8_t *rx, size_t len)
{
    gpio_set_level(cs_pin, 0);
    ets_delay_us(5);
    bb_byte(cmd);
    for (size_t i = 0; i < len; i++) rx[i] = bb_byte(0x00);
    ets_delay_us(5);
    gpio_set_level(cs_pin, 1);
}

static int poll_ready(int cs_pin, int num)
{
    for (int i = 0; i < 150; i++) {
        uint8_t status = 0;
        bb_read(cs_pin, PN532_SPI_STATREAD, &status, 1);
        if (i < 5 || (i % 20 == 0))
            printf("[PN532 #%d] status[%d] = 0x%02X\n", num, i, status);
        if (status == 0x01) return 1;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    printf("[PN532 #%d] status[last] = (all same)\n", num);
    return 0;
}

static void probe(int cs_pin, int num)
{
    printf("[PN532 #%d] sending GetFirmwareVersion\n", num);

    uint8_t wbuf[sizeof(cmd_get_fw) + 1];
    wbuf[0] = PN532_SPI_DATAWRITE;
    memcpy(wbuf + 1, cmd_get_fw, sizeof(cmd_get_fw));
    bb_write(cs_pin, wbuf, sizeof(wbuf));

    if (!poll_ready(cs_pin, num)) {
        printf("[PN532 #%d] FAIL — no ACK\n", num);
        return;
    }

    uint8_t ack[6] = {0};
    bb_read(cs_pin, PN532_SPI_DATAREAD, ack, 6);
    printf("[PN532 #%d] ack:", num);
    for (int i = 0; i < 6; i++) printf(" %02X", ack[i]);
    printf("\n");

    if (!poll_ready(cs_pin, num)) {
        printf("[PN532 #%d] FAIL — no response\n", num);
        return;
    }

    uint8_t resp[20] = {0};
    bb_read(cs_pin, PN532_SPI_DATAREAD, resp, 20);
    printf("[PN532 #%d] raw:", num);
    for (int i = 0; i < 20; i++) printf(" %02X", resp[i]);
    printf("\n");

    for (int i = 0; i < 14; i++) {
        if (resp[i] == 0xD5 && resp[i + 1] == 0x03) {
            printf("[PN532 #%d] OK  IC=0x%02X  Ver=%d  Rev=%d  Support=0x%02X\n",
                   num, resp[i + 2], resp[i + 3], resp[i + 4], resp[i + 5]);
            return;
        }
    }
    printf("[PN532 #%d] FAIL — D5 03 not found\n", num);
}

void app_main(void)
{
    printf("\n=== Phase 0 SPI smoke test (bit-bang) ===\n");

    gpio_config_t out_cfg = {
        .pin_bit_mask  = (1ULL << PIN_MOSI) | (1ULL << PIN_SCK)
                       | (1ULL << PIN_CS1)  | (1ULL << PIN_CS2),
        .mode          = GPIO_MODE_OUTPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    gpio_config(&out_cfg);

    gpio_config_t in_cfg = {
        .pin_bit_mask  = (1ULL << PIN_MISO),
        .mode          = GPIO_MODE_INPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    gpio_config(&in_cfg);

    gpio_set_level(PIN_CS1,  1);
    gpio_set_level(PIN_CS2,  1);
    gpio_set_level(PIN_SCK,  0);
    gpio_set_level(PIN_MOSI, 0);

    printf("GPIO init done — MISO raw = %d\n", gpio_get_level(PIN_MISO));

    vTaskDelay(pdMS_TO_TICKS(2000));

    printf("MISO after 2s delay = %d\n", gpio_get_level(PIN_MISO));

    probe(PIN_CS1, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    probe(PIN_CS2, 2);

    printf("=== Done ===\n");
}
