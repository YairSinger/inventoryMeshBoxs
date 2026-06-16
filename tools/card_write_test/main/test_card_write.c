/* Card write/verify test — writes "card1", "card2" NDEF text records to
 * tags on READER 1 (outer, CS GPIO9), two stages per card:
 *   1) write  — bring card to reader, write its identifier
 *   2) verify — remove + bring card back, read it back and compare
 */
#include "imb_nfc.h"
#include "imb_nfc_pn532.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

#define NFC_MOSI  11
#define NFC_MISO  13
#define NFC_SCK   12
#define NFC_CS0   10   /* inner reader */
#define NFC_CS1    9   /* outer reader */

#define READER_OUTER 1

#define POLL_MS     50
#define TAG_WAIT_MS 30000

static void prompt(const char *msg)
{
    printf("\n\n=== ACTION REQUIRED ===\n>>> %s\n=======================\n\n", msg);
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(2000));
}

static int wait_for_tag(uint8_t reader_id, imb_nfc_tag_t *out, int timeout_ms)
{
    for (int elapsed = 0; elapsed < timeout_ms; elapsed += POLL_MS) {
        if (imb_nfc_scan(reader_id, out)) return 1;
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
    return 0;
}

static int wait_for_no_tag(uint8_t reader_id, int timeout_ms)
{
    imb_nfc_tag_t t;
    for (int elapsed = 0; elapsed < timeout_ms; elapsed += POLL_MS) {
        if (!imb_nfc_scan(reader_id, &t)) return 1;
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
    return 0;
}

static int write_and_verify(const char *name)
{
    imb_nfc_tag_t t;
    char msg[96];

    snprintf(msg, sizeof(msg), "Bring '%s' to READER 1 (outer, CS GPIO%d) to WRITE its name.", name, NFC_CS1);
    prompt(msg);

    if (!wait_for_tag(READER_OUTER, &t, TAG_WAIT_MS)) {
        printf("  FAIL: no tag detected (write stage)\n");
        return 0;
    }
    printf("  UID=%s  SAK=0x%02X\n", t.uid_str, t.sak);

    if (!imb_nfc_write_ndef(READER_OUTER, &t, name)) {
        printf("  FAIL: write_ndef failed for '%s'\n", name);
        return 0;
    }
    printf("  WRITE OK: '%s' written to UID=%s\n", name, t.uid_str);

    snprintf(msg, sizeof(msg), "Remove the card, then bring it back to READER 1 to VERIFY '%s'.", name);
    prompt(msg);

    wait_for_no_tag(READER_OUTER, TAG_WAIT_MS);

    if (!wait_for_tag(READER_OUTER, &t, TAG_WAIT_MS)) {
        printf("  FAIL: no tag detected (verify stage)\n");
        return 0;
    }

    char readback[IMB_NFC_NAME_MAX];
    if (!imb_nfc_read_ndef(READER_OUTER, &t, readback, sizeof(readback))) {
        printf("  FAIL: read_ndef failed for '%s'\n", name);
        return 0;
    }

    if (strcmp(readback, name) != 0) {
        printf("  FAIL: readback '%s' != expected '%s'\n", readback, name);
        return 0;
    }

    printf("  VERIFY OK: '%s' confirmed on UID=%s\n\n", name, t.uid_str);
    return 1;
}

void app_main(void)
{
    printf("\n\n========================================\n");
    printf("  card write/verify test -- READER 1 (outer, CS GPIO%d)\n", NFC_CS1);
    printf("========================================\n\n");

    imb_nfc_pn532_config_t cfg = {
        .mosi = NFC_MOSI,
        .miso = NFC_MISO,
        .sck  = NFC_SCK,
        .cs   = { NFC_CS0, NFC_CS1 },
    };
    imb_nfc_hal_t hal = imb_nfc_pn532_init(&cfg);
    imb_nfc_init(&hal);
    printf("PN532 init done -- both readers awake.\n\n");

    const char *names[] = { "card3", "card4", "card5", "card6", "card7", "card8", "card9" };
    int total = (int)(sizeof(names) / sizeof(names[0]));
    int pass = 0;

    for (int i = 0; i < total; i++) {
        if (write_and_verify(names[i])) pass++;
    }

    printf("\n========================================\n");
    printf("  RESULT: %d / %d cards written and verified\n", pass, total);
    printf("========================================\n\n");
}
