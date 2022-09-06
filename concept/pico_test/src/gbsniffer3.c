//GPIO23 - LED RGB WS2812 (NeoPixelConnect)
//GPIO25 - LED_BUILTIN

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/timer.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/spi.h"

#include "hardware/resets.h"
#include "hardware/clocks.h"

#define USE_MULTICORE 1

#define LED_PIN         25
#define LED_SET(A)      (gpio_put(LED_PIN, (A)))
#define LED_ON          LED_SET(true)
#define LED_OFF         LED_SET(false)
#define LED_TOGGLE      (gpio_put(LED_PIN, !gpio_get(LED_PIN)))

// SPI mode
#define SPI_PORT        spi0
#define SPI_BAUDRATE    64 * 1024 * 8
#define PIN_SPI_SIN     16 //0 -- just set to 4 during the debug process
#define PIN_SPI_SCK     18 //2
#define PIN_SPI_SOUT    19 //3 

#define MKS(A)                  (A)
#define MS(A)                   ((A) * 1000)
#define SEC(A)                  ((A) * 1000 * 1000)
 
bool speed_240_MHz = false;
volatile uint64_t time_us_now = 0;
volatile uint64_t last_watchdog;

//Test array data
uint8_t image_data[8] = {9,8,7,6,5,4,3,2};
int array_pointer = 0;

static uint8_t set_initial_data() {
    array_pointer = 0;
    return image_data[array_pointer++];
}
static uint8_t process_data(uint8_t data_in) {
    //print_data[print_pointer++] = data_in; //Print the received data
    if (array_pointer >= 8){
        array_pointer = 0;
    }
    return image_data[array_pointer++];
}

static void gpio_callback(uint gpio, uint32_t events) {
    static uint8_t recv_data = 0;
    static uint8_t send_data = 0;
    static uint8_t recv_bits = 0;

    // on the falling edge set sending bit
    if (events & GPIO_IRQ_EDGE_FALL) {
        gpio_put(PIN_SPI_SOUT, send_data), send_data <<= 1;
        return;
    }
    // on the rising edge read received bit
    recv_data = (recv_data << 1) | (gpio_get(PIN_SPI_SIN) & 0x01);

    time_us_now = time_us_64();
    if ((time_us_now - last_watchdog) > SEC(15)) {
        LED_OFF;
        last_watchdog = time_us_now;
    }

    if (++recv_bits != 8) return;
    recv_bits = 0;
    send_data = process_data(recv_data);
}

static inline void setup_sio() {
    // init SIO pins
    gpio_init(PIN_SPI_SCK), gpio_pull_up(PIN_SPI_SCK);
    gpio_set_dir(PIN_SPI_SCK, GPIO_IN);

    gpio_init(PIN_SPI_SIN);
    gpio_set_dir(PIN_SPI_SIN, GPIO_IN);

    gpio_init(PIN_SPI_SOUT);
    gpio_set_dir(PIN_SPI_SOUT, GPIO_OUT);
    gpio_put(PIN_SPI_SOUT, 0);
}

void core1_context() {
    irq_set_mask_enabled(0xffffffff, false);
    
    setup_sio();
    bool new, old = gpio_get(PIN_SPI_SCK);
    while (true) {
        if ((new = gpio_get(PIN_SPI_SCK)) != old) {
            gpio_callback(PIN_SPI_SCK, (new) ? GPIO_IRQ_EDGE_RISE : GPIO_IRQ_EDGE_FALL);
            old = new;
        }
    }
}

int main() {
    speed_240_MHz = set_sys_clock_khz(240000, false);

    // Enable UART so we can print
    stdio_init_all();

    // For toggle_led
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    LED_ON;

    multicore_launch_core1(core1_context);

    LED_OFF;

    while (true) {
        sleep_ms(1000);
    }
}
