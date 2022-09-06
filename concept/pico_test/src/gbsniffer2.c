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
bool firstDataSet = false;

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

static inline void trigger_spi(spi_inst_t *spi, bool mode) {
   
    if(mode){
        // Initialize SPI pins (only first time)
        gpio_set_function(PIN_SPI_SCK, GPIO_FUNC_SPI), gpio_pull_up(PIN_SPI_SCK);
        gpio_set_function(PIN_SPI_SIN, GPIO_FUNC_SPI);
        gpio_set_function(PIN_SPI_SOUT, GPIO_FUNC_SPI);        
    }

    reset_block(spi == spi0 ? RESETS_RESET_SPI0_BITS : RESETS_RESET_SPI1_BITS);
    unreset_block_wait(spi == spi0 ? RESETS_RESET_SPI0_BITS : RESETS_RESET_SPI1_BITS);

    spi_set_baudrate(spi, baudrate);
    spi_set_format(spi, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
    hw_set_bits(&spi_get_hw(spi)->dmacr, SPI_SSPDMACR_TXDMAE_BITS | SPI_SSPDMACR_RXDMAE_BITS);
    spi_set_slave(spi, true);

    hw_set_bits(&spi_get_hw(spi)->cr1, SPI_SSPCR1_SSE_BITS);
    
    // Set the first data into the buffer
    //spi_get_hw(spi)->dr = set_initial_data();        
}


void core1_context() {
    irq_set_mask_enabled(0xffffffff, false);
    //Enable SPI
    trigger_spi(SPI_PORT,true);

    uint64_t last_readable = time_us_64();
    firstDataSet = true; //Just make sure to set the data only one

    while (true) {
        time_us_now = time_us_64();
        if (spi_is_readable(SPI_PORT)) {
            last_readable = time_us_now;
            if (firstDataSet){
                firstDataSet = false;
            }
            spi_get_hw(SPI_PORT)->dr = process_data(spi_get_hw(SPI_PORT)->dr);
        }
        if (time_us_now - last_readable > MS(200) && !firstDataSet) {
            trigger_spi(SPI_PORT,false);
            last_readable = time_us_now;
            firstDataSet = true;
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
