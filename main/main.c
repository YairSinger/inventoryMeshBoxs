/* Buzzer Integration Test
 * Cycles through all imb_buzzer patterns, printing each name via serial.
 * Wiring: GPIO 17 → buzzer +, GND → buzzer –
 * Flash then monitor: python3 tools/serial_monitor.py */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "imb_buzzer.h"
#include "imb_buzzer_ledc.h"

static void delay_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

void app_main(void)
{
    printf("\n=== IMB Buzzer Integration Test (GPIO 17) ===\n");
    printf("Listen for distinct audio pattern per event.\n\n");

    imb_buzzer_hal_t hal = imb_buzzer_ledc_init();
    imb_buzzer_init(&hal);

    while (1) {
        printf("[1/6] TAG_PLACED — single short beep (50ms, 2700Hz)\n");
        imb_buzzer_play(IMB_BUZZ_TAG_PLACED);
        delay_ms(500);

        printf("[2/6] ITEM_REMOVED — two short beeps (50ms+gap+50ms, 2700Hz)\n");
        imb_buzzer_play(IMB_BUZZ_ITEM_REMOVED);
        delay_ms(500);

        printf("[3/6] UNKNOWN_TAG — long warning beep (300ms, 1800Hz)\n");
        imb_buzzer_play(IMB_BUZZ_UNKNOWN_TAG);
        delay_ms(800);

        printf("[4/6] ERROR — three rapid beeps (80ms+gap x3, 1000Hz)\n");
        imb_buzzer_play(IMB_BUZZ_ERROR);
        delay_ms(800);

        printf("[5/6] BLE_CONNECTED — rising chirp (1500Hz then 2500Hz)\n");
        imb_buzzer_play(IMB_BUZZ_BLE_CONNECTED);
        delay_ms(600);

        printf("[6/6] FACTORY_RESET — continuous 800Hz for 2s then silenced\n");
        imb_buzzer_play(IMB_BUZZ_FACTORY_RESET);
        delay_ms(2000);
        imb_buzzer_silence();
        delay_ms(500);

        printf("\n--- Cycle complete ---\n\n");
        delay_ms(1000);
    }
}
