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

struct mobile_adapter adapter;

//SPDR = mobile_transfer(&adapter, SPDR); // how arduino handle the data

uint8_t config_eeprom[FLASH_DATA_SIZE] = {};
bool haveAdapterConfig = false;
bool haveWifiConfig = false;


void main(){
    stdio_init_all();

    uint8_t setConfig = ReadFlashConfig(config_eeprom); 
    switch (setConfig){
        case 0:
            haveAdapterConfig = false;
            haveWifiConfig = false;
        break;
        case 1:
            haveAdapterConfig = true;
            haveWifiConfig = false;
        break;
        case 2:
            haveAdapterConfig = true;
            haveWifiConfig = true;
        break;
        case 3:
            haveAdapterConfig = false;
            haveWifiConfig = true;
        break;
        default:
        break;
    }  

    //mobile_init(&adapter, NULL, NULL);

    while (true) {
        //mobile_loop(&adapter);
    }

}