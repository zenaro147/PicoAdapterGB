#pragma once
#include <stdbool.h>

// Boot/error indicator on the single LED (wired to the cyw43 Wi-Fi chip, not
// the RP2040 itself). Behavior:
//   - solid ON from the moment boot starts, until boot finishes (either
//     normal operation or the fallback hotspot is up and serving the page).
//   - a numbered blink pattern (BIOS-beep-code style) reports a real error
//     partway through boot, then returns to solid ON so "still booting"
//     remains visible until led_status_boot_done() is called.
// Codes must only ever represent errors, never ongoing/expected status (e.g.
// "waiting for the Game Boy" is not an error and must not be signaled here).
enum led_error_code {
    LED_ERROR_WIFI_CONNECT_FAILED = 1, // Could not join the saved Wi-Fi network
    LED_ERROR_WIFI_BADAUTH        = 2, // Wi-Fi network rejected the password
    LED_ERROR_FLASH_SAVE_FAILED   = 3, // Configuration failed to persist to flash
};

// Call once, as the very first thing in main(). Turns the LED on solid.
void led_status_boot_start(void);

// Call once boot has fully finished: either the adapter is entering its
// normal operating loop, or the fallback hotspot is up and its web server is
// listening. Turns the LED off.
void led_status_boot_done(void);

// Blinks the given error code a few times (N blinks, short pause, repeated
// 3 times), then leaves the LED solid ON. Callers must only invoke this for
// an actual error, and only while it's safe to touch the LED (i.e. not in
// the middle of a Wi-Fi radio state transition).
void led_status_report_error(enum led_error_code code);
