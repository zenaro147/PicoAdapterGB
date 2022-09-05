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
//https://www.makermatrix.com/blog/read-and-write-data-with-the-pi-pico-onboard-flash/

//God, what I need to do now? =(


struct mobile_adapter adapter;

//SPDR = mobile_transfer(&adapter, SPDR);
void main(){

    //mobile_init(&adapter, NULL, NULL);

    while (true) {
        sleep_ms(1000);
        //mobile_loop(&adapter);
    }

}