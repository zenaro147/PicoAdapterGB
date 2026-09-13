// SPDX-License-Identifier: GPL-3.0-only
// core/led_hal.h backend for the picow implementation: the Pico W/Pico 2 W's
// single LED is wired to the CYW43 Wi-Fi chip, not to an RP2040/RP2350 GPIO
// directly, so it can only be driven through the cyw43_arch calls below.
#include "core/led_hal.h"

#include "pico/cyw43_arch.h"

void led_hal_set(bool on){
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
}

bool led_hal_get(void){
    return cyw43_arch_gpio_get(CYW43_WL_GPIO_LED_PIN);
}
