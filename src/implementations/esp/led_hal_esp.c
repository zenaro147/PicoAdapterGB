// Plain-GPIO implementation of core/led_hal.h. Unlike picow (whose LED is a
// pin on the CYW43 radio, reachable only through cyw43_arch_gpio_*()), a
// bare pico/pico2 board has its status LED on a normal RP2040/RP2350 GPIO,
// so this just wraps the SDK's own gpio_* calls.
#include "core/led_hal.h"

#include "pico/stdlib.h"

#ifndef PICO_DEFAULT_LED_PIN
#error "PICOADAPTER_IMPLEMENTATION=esp requires a board with PICO_DEFAULT_LED_PIN (pico, pico2)."
#endif

static bool led_state = false;
static bool led_initialized = false;

static void led_hal_ensure_init(void){
    if (led_initialized) return;
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    led_initialized = true;
}

void led_hal_set(bool on){
    led_hal_ensure_init();
    led_state = on;
    gpio_put(PICO_DEFAULT_LED_PIN, on);
}

bool led_hal_get(void){
    return led_state;
}
