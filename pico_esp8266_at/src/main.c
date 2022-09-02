#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/timer.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/uart.h"
#include "hardware/spi.h"
#include "hardware/flash.h"

#include "libmobile/mobile.h"
#include "libmobile/inet_pton.h"

//Flash reprograming example https://github.com/raspberrypi/pico-examples/blob/master/flash/program/flash_program.c

//God, what I need to do now? =(

void main(){
    while (true) {
        sleep_ms(1000);
    }
}