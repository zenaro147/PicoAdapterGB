#include "led_status.h"

#include "globals.h"

#define LED_BLINK_ON_MS    200
#define LED_BLINK_OFF_MS   250
#define LED_BLINK_GAP_MS   700
#define LED_BLINK_REPEATS  3

void led_status_boot_start(void){
    LED_ON;
}

void led_status_boot_done(void){
    LED_OFF;
}

void led_status_report_error(enum led_error_code code){
    for (int repeat = 0; repeat < LED_BLINK_REPEATS; repeat++){
        for (int blink = 0; blink < (int)code; blink++){
            LED_ON;
            busy_wait_us(MS(LED_BLINK_ON_MS));
            LED_OFF;
            busy_wait_us(MS(LED_BLINK_OFF_MS));
        }
        busy_wait_us(MS(LED_BLINK_GAP_MS));
    }
    // Return to solid ON: boot is still in progress after this error.
    LED_ON;
}
