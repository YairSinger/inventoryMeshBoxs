#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"
#include "nvs_flash.h"
#include "imb_ble.h"
#include "esp_log.h"

static const char *TAG = "MAIN";

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

/* InListPassiveTarget: MaxTg=1, BrTy=0x00 (106 kbps ISO14443A = NTAG213) */
static const uint8_t cmd_passive_target[] = {
    0x00, 0x00, 0xFF, 0x04, 0xFC,   /* preamble, len=4, lcs */
    0xD4, 0x4A, 0x01, 0x00,          /* TFI, InListPassiveTarget, MaxTg=1, BrTy=TypeA */
    0xE1, 0x00                        /* DCS, postamble */
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

static void probe(int cs_pin, int num)
{
    printf("[PN532 #%d] == probe start ==\n", num);

    gpio_set_level(cs_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(cs_pin, 1);
    ets_delay_us(1000);

    uint8_t wbuf[sizeof(cmd_get_fw) + 1];
    wbuf[0] = PN532_SPI_DATAWRITE;
    memcpy(wbuf + 1, cmd_get_fw, sizeof(cmd_get_fw));
    bb_write(cs_pin, wbuf, sizeof(wbuf));
    printf("[PN532 #%d] command sent\n", num);

    int ready = 0;
    for (int i = 0; i < 100; i++) {
        uint8_t opcode_miso = 0, status = 0;
        opcode_miso = bb_read_ex(cs_pin, PN532_SPI_STATREAD, &status, 1);

        if (status == 0x01) {
            ready = 1;
            break;
        }
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

    for (int i = 0; i < 100; i++) {
        uint8_t status = 0;
        bb_read(cs_pin, PN532_SPI_STATREAD, &status, 1);
        if (status & 0x01) break;
        vTaskDelay(pdMS_TO_TICKS(5));
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
}

static void scan_tag(int cs_pin, int num)
{
    uint8_t wbuf[sizeof(cmd_passive_target) + 1];
    wbuf[0] = PN532_SPI_DATAWRITE;
    memcpy(wbuf + 1, cmd_passive_target, sizeof(cmd_passive_target));
    bb_write(cs_pin, wbuf, sizeof(wbuf));

    /* wait for ACK */
    int ready = 0;
    for (int i = 0; i < 50; i++) {
        uint8_t s = 0;
        bb_read(cs_pin, PN532_SPI_STATREAD, &s, 1);
        if (s == 0x01) { ready = 1; break; }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (!ready) return;

    uint8_t ack[6] = {0};
    bb_read(cs_pin, PN532_SPI_DATAREAD, ack, 6);

    /* wait for response — tag detection can take ~30ms */
    ready = 0;
    for (int i = 0; i < 50; i++) {
        uint8_t s = 0;
        bb_read(cs_pin, PN532_SPI_STATREAD, &s, 1);
        if (s == 0x01) { ready = 1; break; }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (!ready) return;

    uint8_t resp[32] = {0};
    bb_read(cs_pin, PN532_SPI_DATAREAD, resp, sizeof(resp));

    /* response: ... D5 4B NbTg Tg ATQA(2) SAK NFCIDLen UID... */
    for (int i = 0; i < 28; i++) {
        if (resp[i] == 0xD5 && resp[i+1] == 0x4B) {
            if (resp[i+2] == 0) return; /* NbTg=0, no tag in field */
            uint8_t uid_len = resp[i+7];
            printf("[PN532 #%d] TAG  ATQA=%02X%02X SAK=%02X UID(%d):",
                   num, resp[i+4], resp[i+5], resp[i+6], uid_len);
            for (int j = 0; j < uid_len && j < 10; j++)
                printf(" %02X", resp[i+8+j]);
            printf("\n");
            return;
        }
    }
    /* D5 4B not found — print raw for diagnosis */
    printf("[PN532 #%d] scan raw:", num);
    for (int i = 0; i < 20; i++) printf(" %02X", resp[i]);
    printf("\n");
}

static void on_ble_cmd(const imb_cmd_header_t *hdr, const void *payload)
{
    ESP_LOGI(TAG, "BLE Command Received: type=0x%02X id=%d", hdr->msg_type, hdr->msg_id);
    
    switch (hdr->msg_type) {
        case IMB_MSG_CMD_MODE: {
            const imb_pkt_cmd_mode_t *m = (const imb_pkt_cmd_mode_t *)hdr;
            ESP_LOGI(TAG, "Mode change requested: %d", m->mode);
            imb_ble_set_mode((imb_op_mode_e)m->mode);
            imb_ble_send_ack(hdr->msg_id, hdr->msg_type, IMB_ACK_OK);
            break;
        }
        default:
            ESP_LOGW(TAG, "Unhandled command: 0x%02X", hdr->msg_type);
            imb_ble_send_ack(hdr->msg_id, hdr->msg_type, IMB_ACK_OK);
            break;
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Initializing BLE...");
    imb_ble_config_t ble_cfg = {
        .box_name = "Kitchen",
        .pin_hash = 0x12345678, /* matches phone test */
        .initial_mode = IMB_MODE_FIELD_CHECK,
        .on_cmd = on_ble_cmd
    };
    ESP_ERROR_CHECK(imb_ble_init(&ble_cfg));

    /* GPIO Setup for PN532 */
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

    ESP_LOGI(TAG, "System Ready. Scanning for tags...");

    while (1) {
        scan_tag(PIN_CS1, 1);
        scan_tag(PIN_CS2, 2);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
