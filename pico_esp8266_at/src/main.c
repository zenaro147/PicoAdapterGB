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

#include "esp_at.h"
#include "flash_eeprom.h"
#include "libmobile/mobile.h"
#include "libmobile/inet_pton.h"

//Flash reprograming example https://github.com/raspberrypi/pico-examples/blob/master/flash/program/flash_program.c
//https://www.makermatrix.com/blog/read-and-write-data-with-the-pi-pico-onboard-flash/

struct mobile_adapter adapter;

//SPDR = mobile_transfer(&adapter, SPDR); // how arduino handle the data

//Need to write the config file into flash (192 bytes)

void main(){

    //mobile_init(&adapter, NULL, NULL);

    while (true) {
        //mobile_loop(&adapter);
    }

}