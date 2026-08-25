#pragma once
#include <stdbool.h>

// Single-LED control surface used by globals.h's LED_ON/LED_OFF/LED_TOGGLE
// macros and core/led_status.c. Which peripheral this actually drives (a
// GPIO on the RP2040/RP2350 itself, or a pin on an attached radio like the
// CYW43) is entirely up to the selected implementation (see
// src/implementations/<impl>/).
void led_hal_set(bool on);
bool led_hal_get(void);
