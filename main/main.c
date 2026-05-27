#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

#define PIN_MOSI  11
#define PIN_MISO  13
#define PIN_SCK   12
#define PIN_CS1   10
#define PIN_CS2    9

#define PN532_SPI_STATREAD  0x02
#define PN532_SPI_DATAWRITE 0x01
#define PN532_SPI_DATAREAD  0x03

static spi_device_handle_t spi;

static const uint8_t cmd_get_fw[] = {
    0x00, 0x00, 0xFF, 0x02, 0xFE, 0xD4, 0x02, 0x2A, 0x00
};

static void spi_txrx(uint8_t *tx, uint8_t *rx, size_t len)
{
    spi_transaction_t t = {
        .length    = len * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    spi_device_polling_transmit(spi, &t);
}

static void probe(int cs_pin, int num)
{
    printf("[PN532 #%d] wakeup + GetFirmwareVersion\n", num);

    /* wakeup: hold SS low for 5ms with NO clock, then release */
    gpio_set_level(cs_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    gpio_set_level(cs_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(2));

    /* send command frame */
    uint8_t wbuf[sizeof(cmd_get_fw) + 1];
    wbuf[0] = PN532_SPI_DATAWRITE;
    memcpy(wbuf + 1, cmd_get_fw, sizeof(cmd_get_fw));
    gpio_set_level(cs_pin, 0);
    spi_txrx(wbuf, NULL, sizeof(wbuf));
    gpio_set_level(cs_pin, 1);

    /* skip status check — just wait 500ms and read directly */
    vTaskDelay(pdMS_TO_TICKS(500));

    /* read response */
    uint8_t dr = PN532_SPI_DATAREAD;
    uint8_t resp[20] = {0};
    gpio_set_level(cs_pin, 0);
    spi_txrx(&dr, NULL, 1);
    spi_txrx(NULL, resp, 20);
    gpio_set_level(cs_pin, 1);

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
    printf("\n=== Phase 0 SPI smoke test (manual CS) ===\n");

    /* CS pins as plain GPIO outputs, default HIGH (deselected) */
    gpio_config_t cs_cfg = {
        .pin_bit_mask  = (1ULL << PIN_CS1) | (1ULL << PIN_CS2),
        .mode          = GPIO_MODE_OUTPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    gpio_config(&cs_cfg);
    gpio_set_level(PIN_CS1, 1);
    gpio_set_level(PIN_CS2, 1);

    /* SPI bus — no auto-CS (spics_io_num = -1) */
    spi_bus_config_t bus = {
        .mosi_io_num   = PIN_MOSI,
        .miso_io_num   = PIN_MISO,
        .sclk_io_num   = PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t cfg = {
        .clock_speed_hz = 500000,          /* 500 kHz — slow for bringup */
        .mode           = 0,               /* CPOL=0 CPHA=0 */
        .spics_io_num   = -1,              /* manual CS */
        .queue_size     = 1,
        .flags          = 0,  /* MSB-first; some PN532 clones bit-reverse internally */
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &cfg, &spi));

    vTaskDelay(pdMS_TO_TICKS(1000));       /* PN532 power-on time */

    probe(PIN_CS1, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    probe(PIN_CS2, 2);

    printf("=== Done ===\n");
}
